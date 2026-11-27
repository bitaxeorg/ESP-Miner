#include "unity.h"
#include "mining_watchdog.h"

// Steps `seconds` times with the input as given, returning the first action
static wd_asic_action_t run_asic(wd_asic_t *wd, uint32_t seconds, const wd_asic_input_t *in)
{
    for (uint32_t s = 0; s < seconds; s++) {
        wd_asic_action_t a = wd_asic_step(wd, in);
        if (a != WD_ASIC_NONE) return a;
    }
    return WD_ASIC_NONE;
}

// Results keep coming but the 1 min hashrate holds the exact same value
static wd_asic_action_t run_frozen(wd_asic_t *wd, uint32_t seconds, wd_asic_input_t *in)
{
    for (uint32_t s = 0; s < seconds; s++) {
        in->results++;
        wd_asic_action_t a = wd_asic_step(wd, in);
        if (a != WD_ASIC_NONE) return a;
    }
    return WD_ASIC_NONE;
}

// Healthy mining: results and a moving hashrate
static wd_asic_action_t run_healthy(wd_asic_t *wd, uint32_t seconds, wd_asic_input_t *in)
{
    for (uint32_t s = 0; s < seconds; s++) {
        in->results++;
        in->hashrate_1m += 0.01f;
        wd_asic_action_t a = wd_asic_step(wd, in);
        if (a != WD_ASIC_NONE) return a;
    }
    return WD_ASIC_NONE;
}

static wd_pool_action_t run_pool(wd_pool_t *wd, uint32_t seconds, uint64_t work, uint64_t sub, uint64_t res)
{
    for (uint32_t s = 0; s < seconds; s++) {
        wd_pool_action_t a = wd_pool_step(wd, true, work, sub, res);
        if (a != WD_POOL_NONE) return a;
    }
    return WD_POOL_NONE;
}

static wd_asic_input_t mining(void)
{
    return (wd_asic_input_t) {.running = true, .has_work = true, .results = 5, .hashrate_1m = 1000.0f, .window_s = 150};
}

TEST_CASE("Window follows the expected result rate", "[mining-watchdog]")
{
    // Gamma at 1.2 TH/s, ASIC difficulty 256: a result every ~0.9 s -> floor
    TEST_ASSERT_EQUAL_UINT32(WD_ASIC_MIN_WINDOW_S, wd_asic_window_s(1200.0, 8));
    // Difficulty 4096 at 1 TH/s: a result every ~17.6 s -> 30 intervals
    TEST_ASSERT_UINT32_WITHIN(1, 527, wd_asic_window_s(1000.0, 12));
    // Difficulty 16384 at 1 TH/s: ~70 s per result -> ~35 min, too slow to watch
    TEST_ASSERT_EQUAL_UINT32(0, wd_asic_window_s(1000.0, 14));
    TEST_ASSERT_EQUAL_UINT32(0, wd_asic_window_s(0.0, 8));
}

TEST_CASE("Healthy mining keeps the ASIC watchdog quiet", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_healthy(&wd, 2 * 3600, &in));
}

TEST_CASE("Silent chain: re-init, then restart, then give up", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, 149, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 1, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REASON_NO_RESULTS, wd.reason);
    // Still nothing after the re-init
    TEST_ASSERT_EQUAL(WD_ASIC_RESTART, run_asic(&wd, 150, &in));

    // After the restart (state carried over), the next attempt is a re-init again
    wd_asic_init(&wd, wd.recoveries, wd.last_action);
    wd_asic_step(&wd, &in);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 150, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_GIVE_UP, run_asic(&wd, 150, &in));
}

TEST_CASE("A re-init that brings results back is not escalated", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 150, &in));
    in.results++;
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 151, &in));
}

TEST_CASE("Budget refills after an hour without recoveries", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, WD_ASIC_MAX_RECOVERIES, WD_ASIC_REINIT);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);
    TEST_ASSERT_EQUAL(WD_ASIC_GIVE_UP, run_asic(&wd, 150, &in));

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_healthy(&wd, WD_ASIC_BUDGET_S, &in));
    TEST_ASSERT_EQUAL_UINT8(0, wd.recoveries);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 150, &in));
}

TEST_CASE("No work, pause or ramp don't count as silence", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, 100, &in));
    in.has_work = false;   // held, not reset
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, 600, &in));
    in.has_work = true;
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 50, &in));

    in.running = false;    // reset
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, 600, &in));
    in.running = true;
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, 149, &in));
}

TEST_CASE("Frozen 1 min hashrate triggers after three minutes", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_frozen(&wd, WD_ASIC_FROZEN_S - 1, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_frozen(&wd, 1, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REASON_FROZEN_HASHRATE, wd.reason);

    // Still frozen after the re-init: restart
    TEST_ASSERT_EQUAL(WD_ASIC_RESTART, run_frozen(&wd, WD_ASIC_FROZEN_S, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REASON_FROZEN_HASHRATE, wd.reason);
}

TEST_CASE("Frozen hashrate that moves again after a re-init is not escalated", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_frozen(&wd, WD_ASIC_FROZEN_S, &in));
    in.hashrate_1m = 1010.0f;
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_frozen(&wd, WD_ASIC_FROZEN_S + 1, &in));
}

TEST_CASE("Frozen hashrate is held without work and reset when not running", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_frozen(&wd, 120, &in));
    in.has_work = false;
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_frozen(&wd, 600, &in));
    in.has_work = true;
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_frozen(&wd, 60, &in));

    in.running = false;
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_frozen(&wd, 600, &in));
    in.running = true;
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_frozen(&wd, WD_ASIC_FROZEN_S - 1, &in));
}

TEST_CASE("No results is reported over a frozen hashrate", "[mining-watchdog]")
{
    wd_asic_t wd;
    wd_asic_init(&wd, 0, WD_ASIC_NONE);
    wd_asic_input_t in = mining();
    wd_asic_step(&wd, &in);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, 150, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REASON_NO_RESULTS, wd.reason);
}

TEST_CASE("Pool without new work triggers after five minutes", "[mining-watchdog]")
{
    wd_pool_t wd;
    wd_pool_init(&wd);
    wd_pool_step(&wd, true, 10, 0, 0);

    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, WD_POOL_NO_WORK_S - 1, 10, 0, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NO_WORK, run_pool(&wd, 1, 10, 0, 0));

    // Work every 30 s keeps it quiet
    for (uint64_t work = 11; work < 31; work++) {
        TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 30, work, 0, 0));
    }
}

TEST_CASE("Shares without any reply trigger after three minutes", "[mining-watchdog]")
{
    wd_pool_t wd;
    wd_pool_init(&wd);
    wd_pool_step(&wd, true, 1, 0, 0);

    // Submitted 3, answered 3: fine
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 1, 2, 3, 3));
    // More keep going out every 5 s, nothing comes back
    uint64_t submitted = 3, work = 2;
    uint32_t seconds = 0;
    wd_pool_action_t a = WD_POOL_NONE;
    while (a == WD_POOL_NONE && seconds < 600) {
        if (seconds % 5 == 0) submitted++;
        if (seconds % 30 == 0) work++;
        a = wd_pool_step(&wd, true, work, submitted, 3);
        seconds++;
    }
    TEST_ASSERT_EQUAL(WD_POOL_NO_REPLIES, a);
    TEST_ASSERT_EQUAL_UINT32(WD_POOL_NO_REPLY_S + 1, seconds);
}

TEST_CASE("Late replies and new sessions clear the reply count", "[mining-watchdog]")
{
    wd_pool_t wd;
    wd_pool_init(&wd);
    wd_pool_step(&wd, true, 1, 0, 0);

    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 1, 2, 2, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 120, 6, 2, 0));
    // A batched reply arrives
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 120, 10, 2, 2));

    // Session reset: counters start over at 0
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 1, 11, 4, 2));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 1, 12, 0, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, 170, 18, 0, 0));
}

TEST_CASE("Inactive pool watchdog never triggers", "[mining-watchdog]")
{
    wd_pool_t wd;
    wd_pool_init(&wd);
    for (int i = 0; i < 3600; i++) {
        TEST_ASSERT_EQUAL(WD_POOL_NONE, wd_pool_step(&wd, false, 0, 9, 0));
    }
    // Becoming active starts the counts from there
    TEST_ASSERT_EQUAL(WD_POOL_NONE, wd_pool_step(&wd, true, 0, 9, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, WD_POOL_NO_WORK_S - 1, 0, 9, 0));
}
