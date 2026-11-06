#include "bzm/hashrate.h"
#include "unity.h"

TEST_CASE("BZM rate includes sparse zero intervals and ages old work out", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    bzm_hashrate_sample(&rate, 0, 1000000, true);
    bzm_hashrate_sample(&rate, 512, 2000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1099.5116f, rate.rate);
    bzm_hashrate_sample(&rate, 512, 3000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 733.0078f, rate.rate);
    for (unsigned second = 4; second <= 62; ++second)
        bzm_hashrate_sample(&rate, 512, (uint64_t)second * 1000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0, rate.rate);
    TEST_ASSERT_TRUE((uint64_t)(512) == (uint64_t)(rate.counter));
}

TEST_CASE("BZM rate preserves difficulty credit past raw-hash and 32-bit counter boundaries", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    uint64_t base = UINT32_MAX - 100;
    bzm_hashrate_sample(&rate, base, 1000000, true);
    bzm_hashrate_sample(&rate, base + 512, 2000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1099.5116f, rate.rate);
    TEST_ASSERT_TRUE((uint64_t)(base + 512) == (uint64_t)(rate.counter));
}

TEST_CASE("BZM stopped samples preserve history and resume with a fresh counter baseline", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    bzm_hashrate_sample(&rate, 0, 1000000, true);
    bzm_hashrate_sample(&rate, 512, 2000000, true);
    float previous = rate.rate;
    bzm_hashrate_sample(&rate, 512, 2500000, false);
    TEST_ASSERT_EQUAL_FLOAT(previous, rate.rate);
    bzm_hashrate_sample(&rate, 0, 3000000, false);
    bzm_hashrate_sample(&rate, 0, 4000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 549.7558f, rate.rate);
    bzm_hashrate_sample(&rate, 256, 5000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 659.7070f, rate.rate);
    bzm_hashrate_sample(&rate, 256, 65000000, false);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0, rate.rate);
}

TEST_CASE("BZM sampling uses elapsed time and does not append duplicate reads", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    bzm_hashrate_sample(&rate, 0, 1000000, true);
    bzm_hashrate_sample(&rate, 1024, 3000000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1466.0155f, rate.rate);
    float previous = rate.rate;
    unsigned count = rate.count;
    bzm_hashrate_sample(&rate, 1024, 3000000, true);
    bzm_hashrate_sample(&rate, 1024, 3500000, true);
    TEST_ASSERT_EQUAL_FLOAT(previous, rate.rate);
    TEST_ASSERT_EQUAL_UINT(count, rate.count);
}
