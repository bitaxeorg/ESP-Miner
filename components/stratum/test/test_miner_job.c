#include "unity.h"
#include "miner_job.h"
#include <stdlib.h>
#include <string.h>
#include "esp_psram.h"
#include "esp_heap_caps.h"

TEST_CASE("miner_job_get_slot indexing and buffer validity", "[stratum]")
{
    miner_job_pool_init();

    miner_job_t *slots[MINER_JOB_POOL_SIZE];

    // 1. Verify all slots in the pool are valid and distinct
    for (size_t i = 0; i < MINER_JOB_POOL_SIZE; i++) {
        slots[i] = miner_job_get_slot(i);
        TEST_ASSERT_NOT_NULL(slots[i]);
        TEST_ASSERT_NOT_NULL(slots[i]->coinbase_prefix);
        TEST_ASSERT_NOT_NULL(slots[i]->coinbase_suffix);
        TEST_ASSERT_NOT_EQUAL(slots[i]->coinbase_prefix, slots[i]->coinbase_suffix);
    }

    // Verify distinct slot addresses
    for (size_t i = 0; i < MINER_JOB_POOL_SIZE; i++) {
        for (size_t j = i + 1; j < MINER_JOB_POOL_SIZE; j++) {
            TEST_ASSERT_NOT_EQUAL(slots[i], slots[j]);
            TEST_ASSERT_NOT_EQUAL(slots[i]->coinbase_prefix, slots[j]->coinbase_prefix);
            TEST_ASSERT_NOT_EQUAL(slots[i]->coinbase_suffix, slots[j]->coinbase_suffix);
        }
    }

    // 2. Verify wrap-around indexing
    for (size_t i = 0; i < MINER_JOB_POOL_SIZE * 3; i++) {
        TEST_ASSERT_EQUAL_PTR(slots[i % MINER_JOB_POOL_SIZE], miner_job_get_slot(i));
    }
}


TEST_CASE("miner_job_is_rollable validation", "[stratum]")
{
    miner_job_t *job = miner_job_get_slot(2);
    TEST_ASSERT_FALSE(miner_job_is_rollable(NULL));

    job->extranonce2_len = 0;
    job->coinbase_prefix_len = 10;
    TEST_ASSERT_FALSE(miner_job_is_rollable(job));

    job->extranonce2_len = 4;
    job->coinbase_prefix_len = 0;
    TEST_ASSERT_FALSE(miner_job_is_rollable(job));

    job->extranonce2_len = 4;
    job->coinbase_prefix_len = 10;
    TEST_ASSERT_TRUE(miner_job_is_rollable(job));
}

TEST_CASE("coinbase suffix resize preserves storage on rejected size", "[stratum]")
{
    miner_job_t job = {0};
    TEST_ASSERT_TRUE(miner_job_resize_coinbase_suffix(&job, 32));
    memset(job.coinbase_suffix, 0xa5, 32);
    uint8_t *original = job.coinbase_suffix;
    TEST_ASSERT_FALSE(miner_job_resize_coinbase_suffix(&job, MAX_COINBASE_SUFFIX_LEN + 1U));
    TEST_ASSERT_EQUAL_PTR(original, job.coinbase_suffix);
    TEST_ASSERT_EQUAL_HEX8(0xa5, job.coinbase_suffix[31]);
    TEST_ASSERT_TRUE(miner_job_resize_coinbase_suffix(&job, 4096));
    TEST_ASSERT_EQUAL_HEX8(0xa5, job.coinbase_suffix[31]);
    job.coinbase_suffix[4095] = 0x5a;
    TEST_ASSERT_TRUE(miner_job_resize_coinbase_suffix(&job, 16));
    TEST_ASSERT_EQUAL_HEX8(0xa5, job.coinbase_suffix[15]);
    free(job.coinbase_suffix);
}

TEST_CASE("coinbase suffix allocation failure preserves job and permits recovery", "[stratum]")
{
    if (!esp_psram_is_initialized()) TEST_IGNORE_MESSAGE("PSRAM allocation failure test");
    miner_job_t job = {0};
    TEST_ASSERT_TRUE(miner_job_resize_coinbase_suffix(&job, 32));
    memset(job.coinbase_suffix, 0xa5, 32);
    uint8_t *original = job.coinbase_suffix;
    void *held[128];
    size_t count = 0;
    while (count < sizeof(held) / sizeof(held[0])) {
        void *block = heap_caps_malloc(256 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!block) break;
        held[count++] = block;
    }
    bool resized = miner_job_resize_coinbase_suffix(&job, MAX_COINBASE_SUFFIX_LEN);
    while (count > 0) free(held[--count]);
    TEST_ASSERT_FALSE(resized);
    TEST_ASSERT_EQUAL_PTR(original, job.coinbase_suffix);
    TEST_ASSERT_EQUAL_HEX8(0xa5, job.coinbase_suffix[31]);
    TEST_ASSERT_TRUE(miner_job_resize_coinbase_suffix(&job, MAX_COINBASE_SUFFIX_LEN));
    TEST_ASSERT_EQUAL_HEX8(0xa5, job.coinbase_suffix[31]);
    free(job.coinbase_suffix);
}
