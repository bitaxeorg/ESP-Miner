#include "unity.h"
#include "miner_job.h"

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
