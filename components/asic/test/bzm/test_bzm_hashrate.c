#include "bzm/hashrate.h"
#include "unity.h"

TEST_CASE("BZM rate includes sparse zero intervals and ages old work out", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    float interval;
    TEST_ASSERT_FALSE(bzm_hashrate_sample(&rate, 0, 1000000, &interval));
    TEST_ASSERT_EQUAL_UINT(0, rate.count);
    TEST_ASSERT_TRUE(bzm_hashrate_sample(&rate, 512, 2000000, &interval));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2199.0233f, interval);
    TEST_ASSERT_EQUAL_FLOAT(interval, rate.rate);
    TEST_ASSERT_TRUE(bzm_hashrate_sample(&rate, 512, 3000000, &interval));
    TEST_ASSERT_EQUAL_FLOAT(0, interval);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1099.5116f, rate.rate);
    for (unsigned second = 4; second <= 62; ++second)
        TEST_ASSERT_TRUE(bzm_hashrate_sample(&rate, 512, (uint64_t)second * 1000000, &interval));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0, rate.rate);
    TEST_ASSERT_TRUE((uint64_t)(512) == (uint64_t)(rate.counter));
}

TEST_CASE("BZM rate preserves difficulty credit past raw-hash and 32-bit counter boundaries", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    uint64_t base = UINT32_MAX - 100;
    float interval;
    TEST_ASSERT_FALSE(bzm_hashrate_sample(&rate, base, 1000000, &interval));
    TEST_ASSERT_TRUE(bzm_hashrate_sample(&rate, base + 512, 2000000, &interval));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2199.0233f, rate.rate);
    TEST_ASSERT_TRUE((uint64_t)(base + 512) == (uint64_t)(rate.counter));
}

TEST_CASE("BZM sampling uses elapsed time and does not append duplicate reads", "[asic][bzm][hashrate]")
{
    bzm_hashrate_t rate = {0};
    float interval;
    TEST_ASSERT_FALSE(bzm_hashrate_sample(&rate, 0, 1000000, &interval));
    TEST_ASSERT_TRUE(bzm_hashrate_sample(&rate, 1024, 3000000, &interval));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2199.0233f, rate.rate);
    TEST_ASSERT_FALSE(bzm_hashrate_sample(&rate, 1024, 3000000, &interval));
    TEST_ASSERT_EQUAL_FLOAT(0, interval);
    TEST_ASSERT_EQUAL_UINT(1, rate.count);
    TEST_ASSERT_TRUE(bzm_hashrate_sample(&rate, 1280, 3500000, &interval));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2199.0233f, interval);
    TEST_ASSERT_EQUAL_UINT(2, rate.count);
}
