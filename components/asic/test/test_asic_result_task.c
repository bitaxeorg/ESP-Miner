#include "result_task_test_bindings.h"

#include "asic.h"
#include "asic_common.h"
#include "global_state.h"
#include "hashrate_monitor_task.h"
#include "mining.h"
#include "scoreboard.h"
#include "self_test.h"
#include "stratum_task.h"
#include "system.h"
#include "unity.h"

#include <float.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool paused;
    bool invalid;
    bool missing;
    bool self_test;
    bool replace_during_submit;
    unsigned repeated_results;
    double pool_diff;
    int submit_result;
    uint64_t sent_time;
    miner_job_type_t protocol;
} result_case_t;

static jmp_buf fixture_done;
static GlobalState fixture_state;
static bm_job *fixture_slots[128];
static uint8_t fixture_valid[128];
static task_result fixture_events[2];
static size_t fixture_event_index;
static result_case_t fixture_case;
static unsigned fixture_delays;
static unsigned fixture_submissions;
static unsigned fixture_scores;
static unsigned fixture_notifications;
static unsigned fixture_self_tests;
static unsigned fixture_registers;
static char fixture_scored_id[32];
static char fixture_submitted_id[32];

void result_task_spy_delay(TickType_t ticks)
{
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(100), ticks);
    fixture_delays++;
    fixture_state.ASIC_initalized = true;
}

task_result *result_task_fake_process_work(GlobalState *state)
{
    TEST_ASSERT_EQUAL_PTR(&fixture_state, state);
    if (fixture_event_index++ == 0) {
        return NULL;
    }
    if (fixture_event_index == 2) {
        return &fixture_events[0];
    }
    if (fixture_event_index <= 3 + fixture_case.repeated_results) {
        return &fixture_events[1];
    }
    longjmp(fixture_done, 1);
}

int result_task_fake_submit_share(GlobalState *state, const bm_job *job,
                                  uint32_t nonce, uint32_t version,
                                  uint64_t *sent_time)
{
    TEST_ASSERT_EQUAL_PTR(&fixture_state, state);
    TEST_ASSERT_EQUAL_HEX32(7, nonce);
    TEST_ASSERT_EQUAL_HEX32(0x20000004, version);
    TEST_ASSERT_EQUAL_UINT32(123, job->ntime);
    TEST_ASSERT_EQUAL(fixture_case.protocol, job->job_type);
    TEST_ASSERT_NOT_NULL(job->jobid);
    TEST_ASSERT_NOT_NULL(job->extranonce2);
    snprintf(fixture_submitted_id, sizeof(fixture_submitted_id), "%s",
             job->jobid);

    fixture_submissions++;
    if (fixture_case.replace_during_submit) {
        pthread_mutex_lock(
            &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock);
        free_bm_job(fixture_slots[8]);
        fixture_slots[8] = NULL;
        fixture_valid[8] = 0;
        pthread_mutex_unlock(
            &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock);
    }
    *sent_time = fixture_case.sent_time;
    return fixture_case.submit_result;
}

void result_task_spy_record_nonce(GlobalState *state, double difficulty)
{
    TEST_ASSERT_EQUAL_PTR(&fixture_state, state);
    TEST_ASSERT_TRUE(difficulty > 0);
    fixture_self_tests++;
}

void result_task_spy_notify_found_nonce(GlobalState *state, double difficulty,
                                        uint32_t target)
{
    TEST_ASSERT_EQUAL_PTR(&fixture_state, state);
    TEST_ASSERT_TRUE(difficulty > 0);
    TEST_ASSERT_EQUAL_HEX32(0x1705dd01, target);
    fixture_notifications++;
}

esp_err_t result_task_spy_scoreboard_add(
    Scoreboard *scoreboard, double difficulty, const char *job_id,
    const char *extranonce, uint32_t ntime, uint32_t nonce,
    uint32_t version_bits)
{
    TEST_ASSERT_EQUAL_PTR(&fixture_state.SYSTEM_MODULE.scoreboard, scoreboard);
    TEST_ASSERT_TRUE(difficulty > 0);
    TEST_ASSERT_EQUAL_UINT32(123, ntime);
    TEST_ASSERT_EQUAL_UINT32(7, nonce);
    TEST_ASSERT_EQUAL_UINT32(0, version_bits);
    TEST_ASSERT_EQUAL_STRING("aabb", extranonce);
    snprintf(fixture_scored_id, sizeof(fixture_scored_id), "%s", job_id);
    fixture_scores++;
    return ESP_OK;
}

void result_task_spy_register_read(void *state, register_type_t type,
                                   uint8_t asic_nr, uint32_t value,
                                   uint64_t timestamp)
{
    TEST_ASSERT_EQUAL_PTR(&fixture_state, state);
    TEST_ASSERT_EQUAL(REGISTER_TOTAL_COUNT, type);
    TEST_ASSERT_EQUAL_UINT8(2, asic_nr);
    TEST_ASSERT_EQUAL_UINT32(55, value);
    TEST_ASSERT_TRUE(timestamp == UINT64_C(1000));
    fixture_registers++;
}

static void run_result_case(result_case_t test_case)
{
    fixture_case = test_case;
    memset(&fixture_state, 0, sizeof(fixture_state));
    memset(fixture_slots, 0, sizeof(fixture_slots));
    memset(fixture_valid, 0, sizeof(fixture_valid));
    memset(fixture_events, 0, sizeof(fixture_events));
    fixture_state.ASIC_initalized = !fixture_case.paused;
    fixture_state.SELF_TEST_MODULE.is_active = fixture_case.self_test;
    fixture_state.ASIC_TASK_MODULE.active_jobs = fixture_slots;
    fixture_state.ASIC_TASK_MODULE.valid_jobs = fixture_valid;
    TEST_ASSERT_EQUAL_INT(
        0, pthread_mutex_init(
               &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock, NULL));

    fixture_slots[8] = calloc(1, sizeof(*fixture_slots[8]));
    TEST_ASSERT_NOT_NULL(fixture_slots[8]);
    fixture_slots[8]->version = 0x20000004;
    fixture_slots[8]->ntime = 123;
    fixture_slots[8]->target = 0x1705dd01;
    fixture_slots[8]->pool_diff = fixture_case.pool_diff;
    fixture_slots[8]->job_type = fixture_case.protocol;
    fixture_slots[8]->jobid = strdup("42");
    fixture_slots[8]->extranonce2 = strdup("aabb");
    TEST_ASSERT_NOT_NULL(fixture_slots[8]->jobid);
    TEST_ASSERT_NOT_NULL(fixture_slots[8]->extranonce2);
    fixture_valid[8] = !fixture_case.invalid;
    if (fixture_case.missing) {
        free_bm_job(fixture_slots[8]);
        fixture_slots[8] = NULL;
    }

    fixture_events[0] = (task_result) {
        .register_type = REGISTER_TOTAL_COUNT,
        .asic_nr = 2,
        .value = 55,
        .timestamp_us = 1000,
    };
    fixture_events[1] = (task_result) {
        .job_id = 8,
        .nonce = 7,
        .rolled_version = 0x20000004,
        .timestamp_us = 1000,
    };
    fixture_event_index = 0;
    fixture_delays = 0;
    fixture_submissions = 0;
    fixture_scores = 0;
    fixture_notifications = 0;
    fixture_self_tests = 0;
    fixture_registers = 0;
    fixture_scored_id[0] = '\0';
    fixture_submitted_id[0] = '\0';

    if (setjmp(fixture_done) == 0) {
        ASIC_result_task(&fixture_state);
    }

    TEST_ASSERT_EQUAL_UINT32(1, fixture_registers);
    TEST_ASSERT_EQUAL_UINT32(fixture_case.paused ? 1 : 0, fixture_delays);
    if (fixture_slots[8] != NULL) {
        free_bm_job(fixture_slots[8]);
        fixture_slots[8] = NULL;
    }
    TEST_ASSERT_EQUAL_INT(
        0, pthread_mutex_destroy(
               &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock));
}

TEST_CASE("result task keeps owned snapshots through submission for every protocol",
          "[asic][result][ownership][characterization]")
{
    for (int type = JOB_TYPE_V1; type <= JOB_TYPE_SV2_EXTENDED; ++type) {
        run_result_case((result_case_t) {
            .paused = true,
            .pool_diff = 1e-30,
            .protocol = (miner_job_type_t)type,
            .sent_time = 2000,
            .replace_during_submit = true,
        });
        TEST_ASSERT_EQUAL_UINT32(1, fixture_submissions);
        TEST_ASSERT_EQUAL_UINT32(1, fixture_scores);
        TEST_ASSERT_EQUAL_UINT32(1, fixture_notifications);
        TEST_ASSERT_EQUAL_UINT32(0, fixture_self_tests);
        TEST_ASSERT_EQUAL_STRING("42", fixture_submitted_id);
        TEST_ASSERT_EQUAL_STRING("42", fixture_scored_id);
        TEST_ASSERT_EQUAL_FLOAT(1.0f,
                                fixture_state.SYSTEM_MODULE.process_time);
    }
}

TEST_CASE("result task separates registers and rejects unavailable job slots",
          "[asic][result][job-store][characterization]")
{
    run_result_case((result_case_t) {.invalid = true});
    TEST_ASSERT_EQUAL_UINT32(
        0, fixture_submissions + fixture_scores + fixture_notifications +
               fixture_self_tests);

    run_result_case((result_case_t) {.missing = true});
    TEST_ASSERT_EQUAL_UINT32(
        0, fixture_submissions + fixture_scores + fixture_notifications +
               fixture_self_tests);
}

TEST_CASE("result task preserves thresholds self test and repeated delivery",
          "[asic][result][characterization]")
{
    run_result_case((result_case_t) {.self_test = true});
    TEST_ASSERT_EQUAL_UINT32(1, fixture_self_tests);
    TEST_ASSERT_EQUAL_UINT32(
        0, fixture_submissions + fixture_scores + fixture_notifications);

    const double difficulties[] = {0, DBL_MAX};
    for (size_t index = 0;
         index < sizeof(difficulties) / sizeof(difficulties[0]); ++index) {
        run_result_case(
            (result_case_t) {.pool_diff = difficulties[index]});
        TEST_ASSERT_EQUAL_UINT32(0, fixture_submissions);
        TEST_ASSERT_EQUAL_UINT32(1, fixture_scores);
        TEST_ASSERT_EQUAL_UINT32(1, fixture_notifications);
    }

    run_result_case((result_case_t) {
        .pool_diff = 1e-30,
        .submit_result = -1,
        .sent_time = 2000,
    });
    TEST_ASSERT_EQUAL_UINT32(1, fixture_submissions);
    TEST_ASSERT_EQUAL_UINT32(1, fixture_scores);
    TEST_ASSERT_EQUAL_UINT32(1, fixture_notifications);
    TEST_ASSERT_EQUAL_FLOAT(0, fixture_state.SYSTEM_MODULE.process_time);

    run_result_case((result_case_t) {
        .pool_diff = 1e-30,
        .repeated_results = 1,
    });
    TEST_ASSERT_EQUAL_UINT32(2, fixture_submissions);
    TEST_ASSERT_EQUAL_UINT32(2, fixture_scores);
    TEST_ASSERT_EQUAL_UINT32(2, fixture_notifications);
}
