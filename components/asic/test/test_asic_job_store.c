#include "asic_job_store_test_bindings.h"
#include "asic.h"
#include "bm_job.h"
#include "global_state.h"
#include "mining.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

TEST_CASE("job snapshots retain header fields and metadata after slot replacement",
          "[asic-job][ownership]")
{
    static bm_job *slots[MAX_ASIC_JOBS];
    static uint8_t valid[MAX_ASIC_JOBS];
    static GlobalState state;
    memset(slots, 0, sizeof(slots));
    memset(valid, 0, sizeof(valid));
    state = (GlobalState) {
        .ASIC_TASK_MODULE.active_jobs = slots,
        .ASIC_TASK_MODULE.valid_jobs = valid,
    };
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&state.ASIC_TASK_MODULE.valid_jobs_lock, NULL));
    for (int type = JOB_TYPE_V1; type <= JOB_TYPE_SV2_EXTENDED; ++type) {
        asic_job_t original = {
            .version = 0x20000004, .version_mask = 0x1fffe000,
            .ntime = 0x64658bd8, .nbits = 0x1705dd01,
            .starting_nonce = 0x12345678, .pool_diff = 2048.5, .pool_id = 255,
            .source_type = (mining_job_source_t)type,
        };
        for (unsigned i = 0; i < 32; ++i) {
            original.prev_hash[i] = i;
            original.merkle_root[i] = 64 - i;
        }
        memset(original.job_id, 'j', sizeof(original.job_id) - 1);
        memset(original.extranonce2, 'a', sizeof(original.extranonce2) - 1);
        slots[127] = malloc(sizeof(*slots[127]));
        TEST_ASSERT_NOT_NULL(slots[127]);
        TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&original, 4, slots[127]));
        valid[127] = 1;

        asic_job_t snapshot;
        TEST_ASSERT_TRUE(ASIC_get_job_snapshot(&state, 127, &snapshot));
        TEST_ASSERT_EQUAL_INT(0, pthread_mutex_trylock(&state.ASIC_TASK_MODULE.valid_jobs_lock));
        free_bm_job(slots[127]);
        slots[127] = NULL;
        valid[127] = 0;
        TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&state.ASIC_TASK_MODULE.valid_jobs_lock));

        uint8_t original_header[80], snapshot_header[80];
        asic_job_header(&original, original.starting_nonce, original.version, original_header);
        asic_job_header(&snapshot, snapshot.starting_nonce, snapshot.version, snapshot_header);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(original_header, snapshot_header, sizeof(original_header));
        TEST_ASSERT_EQUAL_HEX32(original.version_mask, snapshot.version_mask);
        TEST_ASSERT_EQUAL_DOUBLE(original.pool_diff, snapshot.pool_diff);
        TEST_ASSERT_EQUAL_UINT8(original.pool_id, snapshot.pool_id);
        TEST_ASSERT_EQUAL_INT(original.source_type, snapshot.source_type);
        TEST_ASSERT_EQUAL_STRING(original.job_id, snapshot.job_id);
        TEST_ASSERT_EQUAL_STRING(original.extranonce2, snapshot.extranonce2);
        TEST_ASSERT_EQUAL_DOUBLE(mining_nonce_difficulty(&original, 7, 0x20002004),
                                 mining_nonce_difficulty(&snapshot, 7, 0x20002004));
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(&state.ASIC_TASK_MODULE.valid_jobs_lock));
}

TEST_CASE("ASIC snapshots reject unavailable jobs without modifying the destination",
          "[asic-job][ownership]")
{
    static bm_job *slots[MAX_ASIC_JOBS];
    static uint8_t valid[MAX_ASIC_JOBS];
    static GlobalState state;
    memset(slots, 0, sizeof(slots));
    memset(valid, 0, sizeof(valid));
    state = (GlobalState) {0};
    asic_job_t output, original;
    memset(&original, 0xa5, sizeof(original));
    memcpy(&output, &original, sizeof(output));
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(NULL, 0, &output));
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 0, NULL));
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 0, &output));
    state.ASIC_TASK_MODULE.active_jobs = slots;
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 0, &output));
    state.ASIC_TASK_MODULE.valid_jobs = valid;
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 128, &output));
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 255, &output));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&state.ASIC_TASK_MODULE.valid_jobs_lock, NULL));
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 0, &output));
    valid[0] = 1;
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 0, &output));
    bm_job incomplete = {0};
    slots[0] = &incomplete;
    TEST_ASSERT_FALSE(ASIC_get_job_snapshot(&state, 0, &output));
    TEST_ASSERT_EQUAL_MEMORY(&original, &output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_trylock(&state.ASIC_TASK_MODULE.valid_jobs_lock));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&state.ASIC_TASK_MODULE.valid_jobs_lock));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(&state.ASIC_TASK_MODULE.valid_jobs_lock));
}

TEST_CASE("Bitmain snapshot conversion rejects oversized metadata without truncation",
          "[asic-job][ownership]")
{
    char long_id[ASIC_JOB_ID_LEN + 1], long_en2[ASIC_JOB_EXTRANONCE2_HEX_SIZE + 1];
    memset(long_id, 'x', sizeof(long_id));
    memset(long_en2, 'a', sizeof(long_en2));
    long_id[sizeof(long_id) - 1] = 0;
    long_en2[sizeof(long_en2) - 1] = 0;
    bm_job source = {.job_id = long_id, .extranonce2 = ""};
    asic_job_t output, original;
    memset(&original, 0xa5, sizeof(original));
    memcpy(&output, &original, sizeof(output));
    TEST_ASSERT_FALSE(bm_job_to_asic_job(NULL, &output));
    TEST_ASSERT_FALSE(bm_job_to_asic_job(&source, NULL));
    TEST_ASSERT_FALSE(bm_job_to_asic_job(&source, &output));
    source.job_id = "";
    source.extranonce2 = long_en2;
    TEST_ASSERT_FALSE(bm_job_to_asic_job(&source, &output));
    source.extranonce2 = NULL;
    TEST_ASSERT_FALSE(bm_job_to_asic_job(&source, &output));
    TEST_ASSERT_EQUAL_MEMORY(&original, &output, sizeof(output));
}
