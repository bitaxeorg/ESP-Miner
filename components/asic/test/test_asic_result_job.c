#include "bm13xx_test_harness.h"
#include "bm1397_test_harness.h"
#include "global_state.h"
#include "mining.h"
#include "utils.h"
#include "unity.h"
#include <string.h>

static asic_job_t make_job(mining_job_source_t source_type)
{
    asic_job_t job = (asic_job_t) {
        .version = 0x20000004, .version_mask = 0x1fffe000,
        .ntime = 0x64658bd8, .nbits = 0x1705dd01,
        .starting_nonce = 0x12345678, .pool_id = UINT8_MAX,
        .source_type = source_type,
    };
    diff_to_target(2048.5, job.pool_target);
    for (unsigned i = 0; i < 32; ++i) {
        job.prev_hash[i] = i;
        job.merkle_root[i] = 64 - i;
    }
    memset(job.job_id, 'j', sizeof(job.job_id) - 1);
    memset(job.extranonce2, 'a', sizeof(job.extranonce2) - 1);
    return job;
}

static void queue_response(bool bm1397, uint8_t wire_job_id, uint32_t nonce)
{
    if (bm1397) {
        uint8_t response[BM1397_HARNESS_RESPONSE_SIZE] = {
            0xaa, 0x55, 0, 0, 0, 0, 0, wire_job_id, 0x80,
        };
        memcpy(response + 2, &nonce, sizeof(nonce));
        bm1397_harness_queue_response(response);
    } else {
        uint8_t response[BM13XX_HARNESS_RESPONSE_SIZE] = {
            0xaa, 0x55, 0, 0, 0, 0, 0, wire_job_id, 0x00, 0x01, 0x80,
        };
        memcpy(response + 2, &nonce, sizeof(nonce));
        bm13xx_harness_queue_response(response);
    }
}

static void replace_slot(GlobalState *state, const asic_job_t *replacement)
{
    // Also proves the decoder released its lock before handing back the result.
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_trylock(&state->job_store.lock));
    if (replacement != NULL) {
        state->job_store.entries[2].job = *replacement;
        state->job_store.entries[2].is_valid = true;
    } else {
        memset(&state->job_store.entries[2].job, 0xa5, sizeof(state->job_store.entries[2].job));
        state->job_store.entries[2].is_valid = false;
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&state->job_store.lock));
}

static void assert_result_job(const asic_job_t *expected, const task_result *result,
                              uint32_t nonce, uint32_t rolled_version)
{
    TEST_ASSERT_EQUAL_INT(REGISTER_INVALID, result->register_type);
    TEST_ASSERT_EQUAL_HEX32(nonce, result->nonce);
    TEST_ASSERT_EQUAL_HEX32(rolled_version, result->rolled_version);
    TEST_ASSERT_EQUAL_HEX32(expected->version, result->job.version);
    TEST_ASSERT_EQUAL_HEX32(expected->version_mask, result->job.version_mask);
    TEST_ASSERT_EQUAL_HEX32(expected->starting_nonce, result->job.starting_nonce);
    TEST_ASSERT_EQUAL_HEX32(expected->ntime, result->job.ntime);
    TEST_ASSERT_EQUAL_HEX32(expected->nbits, result->job.nbits);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected->prev_hash, result->job.prev_hash, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected->merkle_root, result->job.merkle_root, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected->pool_target, result->job.pool_target, 32);
    TEST_ASSERT_EQUAL_UINT8(expected->pool_id, result->job.pool_id);
    TEST_ASSERT_EQUAL_INT(expected->source_type, result->job.source_type);
    uint8_t expected_hash[32];
    uint8_t result_hash[32];
    TEST_ASSERT_TRUE(mining_nonce_hash(expected, nonce, rolled_version, expected_hash));
    TEST_ASSERT_TRUE(mining_nonce_hash(&result->job, result->nonce, result->rolled_version, result_hash));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, result_hash, 32);
}

TEST_CASE("ASIC results preserve complete matched jobs across slot reuse for every driver",
          "[asic][result][ownership]")
{
    for (size_t d = 0; d <= BM13XX_HARNESS_DRIVER_COUNT; ++d) {
        bool bm1397 = d == BM13XX_HARNESS_DRIVER_COUNT;
        GlobalState *state = bm1397 ? bm1397_harness_begin()
                                    : bm13xx_harness_begin(&bm13xx_harness_drivers[d]);
        uint8_t (*init)(GlobalState *) = bm1397 ? bm1397_harness_driver.init : bm13xx_harness_drivers[d].init;
        task_result *(*process_work)(GlobalState *) = bm1397 ? bm1397_harness_driver.process_work : bm13xx_harness_drivers[d].process_work;
        uint8_t wire_job_id = bm1397 ? 0x0b : bm13xx_harness_drivers[d].response_job_id;
        TEST_ASSERT_EQUAL_UINT8(2, init(state));

        for (int type = JOB_TYPE_V1; type <= JOB_TYPE_SV2_EXTENDED; ++type) {
            asic_job_t original = make_job((mining_job_source_t)type);
            if (bm1397) {
                bm1397_harness_install_job(2, &original);
            } else {
                bm13xx_harness_install_job(2, &original);
            }
            uint32_t nonce = 0x61000000 + type * 2;
            uint32_t rolled_version = bm1397 ? 0x20006004 : 0x20002004;
            queue_response(bm1397, wire_job_id, nonce);
            const task_result *result = process_work(state);
            TEST_ASSERT_NOT_NULL(result);

            asic_job_t replacement = (asic_job_t) {
                .version = 0x21000004, .job_id = "replacement", .pool_id = 7,
                .ntime = 123, .nbits = 0x1705ae3a,
            };
            diff_to_target(32.0, replacement.pool_target);
            replace_slot(state, &replacement);
            assert_result_job(&original, result, nonce, rolled_version);

            // A caller can retain its own result across the next receive call.
            task_result saved = *result;
            queue_response(bm1397, wire_job_id, nonce + 1);
            result = process_work(state);
            TEST_ASSERT_NOT_NULL(result);
            assert_result_job(&replacement, result, nonce + 1,
                               bm1397 ? 0x21000004 : 0x21002004);
            replace_slot(state, NULL);
            assert_result_job(&original, &saved, nonce, rolled_version);
        }
        if (bm1397) {
            bm1397_harness_end();
        } else {
            bm13xx_harness_end();
        }
    }
}

TEST_CASE("BM1366 and BM1397 reject response job IDs outside the active slots",
          "[asic][result][job-store]")
{
    const uint8_t invalid_ids[] = {0x83, 0xff};
    for (unsigned bm1397 = 0; bm1397 < 2; ++bm1397) {
        GlobalState *state = bm1397 ? bm1397_harness_begin()
                                    : bm13xx_harness_begin(&bm13xx_harness_drivers[0]);
        uint8_t (*init)(GlobalState *) = bm1397 ? bm1397_harness_driver.init : bm13xx_harness_drivers[0].init;
        task_result *(*process_work)(GlobalState *) = bm1397 ? bm1397_harness_driver.process_work : bm13xx_harness_drivers[0].process_work;
        TEST_ASSERT_EQUAL_UINT8(2, init(state));

        asic_job_t slot0_job = make_job(JOB_TYPE_V1);
        asic_job_t slot15_job = make_job(JOB_TYPE_V1);
        if (bm1397) {
            bm1397_harness_install_job(0, &slot0_job);
            bm1397_harness_install_job(15, &slot15_job);
        } else {
            bm13xx_harness_install_job(0, &slot0_job);
            bm13xx_harness_install_job(15, &slot15_job);
        }

        for (size_t i = 0; i < sizeof(invalid_ids); ++i) {
            queue_response(bm1397, invalid_ids[i], 0x62000000 + i);
            TEST_ASSERT_NULL(process_work(state));
        }
        if (bm1397) {
            bm1397_harness_end();
        } else {
            bm13xx_harness_end();
        }
    }
}

TEST_CASE("BM1397 deduplicates repeated and previously found nonces",
          "[asic][result][bm1397]")
{
    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));

    asic_job_t job = make_job(JOB_TYPE_V1);
    bm1397_harness_install_job(0, &job);
    uint8_t wire_job_id = 0;

    // First nonce should succeed
    queue_response(true, wire_job_id, 0x12345678);
    task_result *res = bm1397_harness_driver.process_work(state);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, res->nonce);

    // Immediate duplicate nonce should be rejected
    queue_response(true, wire_job_id, 0x12345678);
    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));

    // A different nonce should succeed
    queue_response(true, wire_job_id, 0x87654321);
    res = bm1397_harness_driver.process_work(state);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_HEX32(0x87654321, res->nonce);

    // The first nonce re-emitted later should still be rejected (in ring buffer)
    queue_response(true, wire_job_id, 0x12345678);
    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));

    bm1397_harness_end();
}

TEST_CASE("BM1397 software rolled starting_version decodes rolled_version and computes correct version_bits",
          "[asic][bm1397][result-job]")
{
    GlobalState *state = bm1397_harness_begin();

    asic_job_t job = make_job(JOB_TYPE_V1);
    job.version = 0x207D8000;      // Software-rolled version for this midstate batch
    job.version_mask = BIP320_VERSION_ROLLING_MASK;
    bm1397_harness_install_job(0, &job);

    // Wire chip slot encodes slot 0 and midstate index 1
    // (slot << 2) | midstate_index: (0 << 2) | 1 = 1
    uint8_t wire_chip_slot = 1;
    queue_response(true, wire_chip_slot, 0xFC7BDA94);

    task_result *res = bm1397_harness_driver.process_work(state);
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_HEX32(0xFC7BDA94, res->nonce);
    // Rolled version should be packet version incremented by midstate index 1: 0x207D8000 -> 0x207DA000
    TEST_ASSERT_EQUAL_HEX32(0x207DA000, res->rolled_version);
    // Packet job version
    TEST_ASSERT_EQUAL_HEX32(0x207D8000, res->job.version);

    // BIP-320 version bits submitted to pool: rolled_version & version_mask
    uint32_t version_bits = res->rolled_version & res->job.version_mask;
    TEST_ASSERT_EQUAL_HEX32(0x007DA000, version_bits);

    // Pool verification check with base job template version (e.g. 0x207C8000):
    // (pool_template_version & ~mask) | (version_bits & mask) == rolled_version
    uint32_t pool_template_version = 0x207C8000;
    uint32_t pool_reconstructed = (pool_template_version & ~job.version_mask) | (version_bits & job.version_mask);
    TEST_ASSERT_EQUAL_HEX32(res->rolled_version, pool_reconstructed);

    bm1397_harness_end();
}
