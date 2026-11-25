#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"

static bool s_fail_next_task_create = false;

static BaseType_t test_xTaskCreateWithCaps(TaskFunction_t pxTaskCode,
                                           const char *const pcName,
                                           const configSTACK_DEPTH_TYPE usStackDepth,
                                           void *const pvParameters,
                                           UBaseType_t uxPriority,
                                           TaskHandle_t *const pxCreatedTask,
                                           UBaseType_t uxMemoryCaps)
{
    if (s_fail_next_task_create) {
        s_fail_next_task_create = false;
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    return xTaskCreateWithCaps(pxTaskCode, pcName, usStackDepth, pvParameters,
                               uxPriority, pxCreatedTask, uxMemoryCaps);
}

#define xTaskCreateWithCaps test_xTaskCreateWithCaps
#include "../../../main/tasks/stratum_task.c"
#undef xTaskCreateWithCaps

#define MAX_RECORDED_TX 32

typedef struct {
    char job_id[ASIC_JOB_ID_LEN];
    uint32_t nonce;
    uint8_t pool_id;
} recorded_tx_t;

static recorded_tx_t s_recorded_tx[MAX_RECORDED_TX];
static size_t s_recorded_tx_count = 0;
static int s_fake_submit_ret = 0;
static uint64_t s_fake_sent_time_us = 0;

static int fake_driver_submit_share(GlobalState *gs, const asic_job_t *job,
                                    uint32_t nonce, uint32_t rolled_version,
                                    uint64_t *sent_time_us)
{
    (void)gs;
    (void)rolled_version;
    if (s_recorded_tx_count < MAX_RECORDED_TX) {
        snprintf(s_recorded_tx[s_recorded_tx_count].job_id,
                 sizeof(s_recorded_tx[s_recorded_tx_count].job_id),
                 "%s", job->job_id);
        s_recorded_tx[s_recorded_tx_count].nonce = nonce;
        s_recorded_tx[s_recorded_tx_count].pool_id = job->pool_id;
        s_recorded_tx_count++;
    }
    if (sent_time_us && s_fake_submit_ret >= 0) {
        *sent_time_us = s_fake_sent_time_us;
    }
    return s_fake_submit_ret;
}

static esp_err_t fake_driver_run(GlobalState *gs, uint16_t pool_idx)
{
    (void)gs;
    (void)pool_idx;
    return ESP_OK;
}

static void fake_driver_abort(uint16_t pool_idx)
{
    (void)pool_idx;
}

const stratum_driver_t g_stratum_v1_driver = {
    .protocol = STRATUM_PROTOCOL_V1,
    .name = "fake_v1",
    .run = fake_driver_run,
    .abort_connection = fake_driver_abort,
    .submit_share = fake_driver_submit_share,
};

const stratum_driver_t g_stratum_v2_driver = {
    .protocol = STRATUM_PROTOCOL_V2,
    .name = "fake_v2",
    .run = fake_driver_run,
    .abort_connection = fake_driver_abort,
    .submit_share = fake_driver_submit_share,
};

void POOL_set_error(GlobalState *gs, uint16_t pool_idx, const char *err_msg)
{
    (void)gs;
    (void)pool_idx;
    (void)err_msg;
}

void POOL_reset_session(GlobalState *gs, uint16_t pool_idx)
{
    (void)gs;
    (void)pool_idx;
}

void POOL_clear_status(GlobalState *gs, uint16_t pool_idx)
{
    (void)gs;
    (void)pool_idx;
}

bool wifi_is_connected(void)
{
    return true;
}

static void reset_tx_fixture(GlobalState *gs)
{
    if (s_share_tx_queue) {
        vQueueDeleteWithCaps(s_share_tx_queue);
        s_share_tx_queue = NULL;
    }
    s_tx_worker_queue = NULL;
    s_primary_task_handle = NULL;
    s_fallback_task_handle = NULL;
    s_global_state = gs;
    s_share_tx_dropped = 0;
    s_share_tx_failed = 0;
    s_fail_next_task_create = false;
    memset((void *)s_should_reconnect, 0, sizeof(s_should_reconnect));
    memset((void *)s_pool_session_id, 0, sizeof(s_pool_session_id));

    memset(s_recorded_tx, 0, sizeof(s_recorded_tx));
    s_recorded_tx_count = 0;
    s_fake_submit_ret = 0;
    s_fake_sent_time_us = 0;

    memset(gs, 0, sizeof(*gs));
    gs->SYSTEM_MODULE.primary_pool_index = 0;
    gs->SYSTEM_MODULE.secondary_pool_index = 1;
    gs->SYSTEM_MODULE.pools[0].url = "pool.example";
    gs->SYSTEM_MODULE.pools[0].port = 3333;
    gs->SYSTEM_MODULE.pools[0].protocol = STRATUM_PROTOCOL_V1;
}

static asic_job_t make_job(uint8_t pool_id, const char *job_id, mining_job_source_t type)
{
    asic_job_t job;
    memset(&job, 0, sizeof(job));
    job.pool_id = pool_id;
    job.source_type = type;
    snprintf(job.job_id, sizeof(job.job_id), "%s", job_id);
    return job;
}

static size_t drain_tx_queue(GlobalState *gs)
{
    stratum_tx_item_t item;
    size_t drained = 0;
    while (xQueueReceive(s_share_tx_queue, &item, 0) == pdTRUE) {
        stratum_dispatch_share_tx(gs, &item.job, item.nonce, item.rolled_version,
                                  item.found_time_us, item.session_id);
        drained++;
    }
    return drained;
}

TEST_CASE("stratum tx synchronous fallback when queue is NULL", "[stratum][tx_queue]")
{
    GlobalState gs;
    reset_tx_fixture(&gs);

    asic_job_t job = make_job(0, "sync-1", JOB_TYPE_V1);
    s_fake_sent_time_us = 4500;
    s_fake_submit_ret = 0;

    int ret = stratum_submit_share(&gs, &job, 0x1111, 0x20000000, 1000, false);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT32(1, s_recorded_tx_count);
    TEST_ASSERT_EQUAL_STRING("sync-1", s_recorded_tx[0].job_id);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, gs.SYSTEM_MODULE.process_time);

    s_fake_submit_ret = -1;
    ret = stratum_submit_share(&gs, &job, 0x2222, 0x20000000, 2000, false);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    stratum_tx_stats_t stats;
    stratum_get_tx_stats(&stats);
    TEST_ASSERT_EQUAL_UINT32(0, stats.queue_dropped);
    TEST_ASSERT_EQUAL_UINT32(1, stats.tx_failed);
}

TEST_CASE("stratum tx init clears queue when worker task creation fails", "[stratum][tx_queue]")
{
    GlobalState gs;
    reset_tx_fixture(&gs);

    s_fail_next_task_create = true;
    stratum_init_share_tx_queue(&gs);

    TEST_ASSERT_NULL(s_share_tx_queue);
    TEST_ASSERT_NULL(s_tx_worker_queue);

    asic_job_t job = make_job(0, "fallback-after-fail", JOB_TYPE_V1);
    s_fake_sent_time_us = 3000;
    TEST_ASSERT_EQUAL_INT(0, stratum_submit_share(&gs, &job, 0x3333, 0x20000000, 1000, false));
    TEST_ASSERT_EQUAL_UINT32(1, s_recorded_tx_count);
    TEST_ASSERT_EQUAL_STRING("fallback-after-fail", s_recorded_tx[0].job_id);
}

TEST_CASE("stratum tx full queue drops normal shares, counts them, and preserves blocks", "[stratum][tx_queue]")
{
    GlobalState gs;
    reset_tx_fixture(&gs);

    s_share_tx_queue = xQueueCreateWithCaps(SHARE_TX_QUEUE_LEN, sizeof(stratum_tx_item_t), MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(s_share_tx_queue);

    for (uint32_t i = 0; i < SHARE_TX_QUEUE_LEN; i++) {
        char id[16];
        snprintf(id, sizeof(id), "q-%" PRIu32, i);
        asic_job_t job = make_job(0, id, JOB_TYPE_V1);
        TEST_ASSERT_EQUAL_INT(0, stratum_submit_share(&gs, &job, i, 0x20000000, 1000 + i, false));
    }

    // Queue is full: normal share is dropped and counted.
    asic_job_t dropped_job = make_job(0, "dropped", JOB_TYPE_V1);
    TEST_ASSERT_EQUAL_INT(-1, stratum_submit_share(&gs, &dropped_job, 0x9999, 0x20000000, 2000, false));

    stratum_tx_stats_t stats;
    stratum_get_tx_stats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1, stats.queue_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats.tx_failed);

    // Queue is full: block solution bypasses the queue and submits synchronously.
    asic_job_t block_job = make_job(0, "block-sync", JOB_TYPE_V1);
    s_fake_sent_time_us = 6000;
    TEST_ASSERT_EQUAL_INT(0, stratum_submit_share(&gs, &block_job, 0xBEEF, 0x20000000, 2500, true));
    TEST_ASSERT_EQUAL_UINT32(1, s_recorded_tx_count);
    TEST_ASSERT_EQUAL_STRING("block-sync", s_recorded_tx[0].job_id);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, gs.SYSTEM_MODULE.process_time);

    // Drop counter remains 1 (block was not dropped).
    stratum_get_tx_stats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1, stats.queue_dropped);

    // Even when the queue has space, a block bypasses the queue immediately.
    stratum_tx_item_t popped;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(s_share_tx_queue, &popped, 0));
    UBaseType_t waiting_before = uxQueueMessagesWaiting(s_share_tx_queue);

    asic_job_t priority_block = make_job(0, "block-direct", JOB_TYPE_V1);
    TEST_ASSERT_EQUAL_INT(0, stratum_submit_share(&gs, &priority_block, 0xCAFE, 0x20000000, 3000, true));
    TEST_ASSERT_EQUAL_UINT32(2, s_recorded_tx_count);
    TEST_ASSERT_EQUAL_STRING("block-direct", s_recorded_tx[1].job_id);
    TEST_ASSERT_EQUAL_HEX32(0xCAFE, s_recorded_tx[1].nonce);
    TEST_ASSERT_EQUAL_UINT32(waiting_before, uxQueueMessagesWaiting(s_share_tx_queue));

    reset_tx_fixture(&gs);
}

TEST_CASE("stratum tx drops queued shares after reconnect and records post-tx process time", "[stratum][tx_queue]")
{
    GlobalState gs;
    reset_tx_fixture(&gs);

    s_share_tx_queue = xQueueCreateWithCaps(SHARE_TX_QUEUE_LEN, sizeof(stratum_tx_item_t), MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(s_share_tx_queue);

    // Enqueue SV2 share with numeric job_id "3" on initial session.
    asic_job_t old_job = make_job(0, "3", JOB_TYPE_SV2_STANDARD);
    TEST_ASSERT_EQUAL_INT(0, stratum_submit_share(&gs, &old_job, 0xAAAA, 0x20000000, 1000, false));

    // Process time must not be recorded before transmission.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, gs.SYSTEM_MODULE.process_time);

    // Simulate reconnect on pool 0 while share is still queued.
    stratum_abort_pool(0);
    TEST_ASSERT_EQUAL(ESP_OK, run_pool_session(&gs, 0));

    // Enqueue a share from the new session reusing job_id "3".
    asic_job_t new_job = make_job(0, "3", JOB_TYPE_SV2_STANDARD);
    TEST_ASSERT_EQUAL_INT(0, stratum_submit_share(&gs, &new_job, 0xBBBB, 0x20000000, 5000, false));

    // Drain queue: old session share is dropped, new session share is dispatched.
    s_fake_sent_time_us = 12500;
    TEST_ASSERT_EQUAL_UINT32(2, drain_tx_queue(&gs));
    TEST_ASSERT_EQUAL_UINT32(1, s_recorded_tx_count);
    TEST_ASSERT_EQUAL_HEX32(0xBBBB, s_recorded_tx[0].nonce);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, gs.SYSTEM_MODULE.process_time);

    reset_tx_fixture(&gs);
}
