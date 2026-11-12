#include "unity.h"
#include "mining_watchdog.h"

#define START_MS 1000

// Ticks once a second until `ms` have passed, returning the first action seen
static wd_asic_action_t run_asic(wd_asic_t *wd, uint64_t *now, uint32_t ms, const wd_asic_input_t *in)
{
    for (uint32_t t = 0; t < ms; t += 1000) {
        *now += 1000;
        wd_asic_action_t a = wd_asic_step(wd, *now, in);
        if (a != WD_ASIC_NONE) return a;
    }
    return WD_ASIC_NONE;
}

static wd_pool_action_t run_pool(wd_pool_t *wd, uint64_t *now, uint32_t ms, uint64_t work, uint64_t sub, uint64_t res)
{
    for (uint32_t t = 0; t < ms; t += 1000) {
        *now += 1000;
        wd_pool_action_t a = wd_pool_step(wd, *now, true, work, sub, res);
        if (a != WD_POOL_NONE) return a;
    }
    return WD_POOL_NONE;
}

TEST_CASE("Window follows the expected result rate", "[mining-watchdog]")
{
    // Gamma at 1.2 TH/s, ASIC difficulty 256: a result every ~0.9 s -> floor
    TEST_ASSERT_EQUAL_UINT32(WD_ASIC_MIN_WINDOW_MS, wd_asic_window_ms(1200.0, 8));
    // Difficulty 4096 at 1 TH/s: a result every ~17.6 s -> 30 intervals
    TEST_ASSERT_UINT32_WITHIN(1000, 527766, wd_asic_window_ms(1000.0, 12));
    // Difficulty 16384 at 1 TH/s: ~70 s per result -> ~35 min, too slow to watch
    TEST_ASSERT_EQUAL_UINT32(0, wd_asic_window_ms(1000.0, 14));
    TEST_ASSERT_EQUAL_UINT32(0, wd_asic_window_ms(0.0, 8));
}

TEST_CASE("Results keep the ASIC watchdog quiet", "[mining-watchdog]")
{
    wd_asic_t wd;
    uint64_t now = START_MS;
    wd_asic_init(&wd, now, 0, WD_ASIC_NONE);
    wd_asic_input_t in = {.running = true, .has_work = true, .results = 0, .window_ms = 150000};
    for (int i = 0; i < 600; i++) {
        now += 1000;
        if (i % 60 == 0) in.results++;
        TEST_ASSERT_EQUAL(WD_ASIC_NONE, wd_asic_step(&wd, now, &in));
    }
}

TEST_CASE("Silent chain: re-init, then restart, then give up", "[mining-watchdog]")
{
    wd_asic_t wd;
    uint64_t now = START_MS;
    wd_asic_init(&wd, now, 0, WD_ASIC_NONE);
    wd_asic_input_t in = {.running = true, .has_work = true, .results = 5, .window_ms = 150000};
    wd_asic_step(&wd, now, &in);

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, &now, 149000, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, &now, 1000, &in));
    // Still nothing after the re-init
    TEST_ASSERT_EQUAL(WD_ASIC_RESTART, run_asic(&wd, &now, 150000, &in));

    // After the restart (state carried over), the next attempt is a re-init again
    wd_asic_init(&wd, now, wd.recoveries, wd.last_action);
    wd_asic_step(&wd, now, &in);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, &now, 150000, &in));
    TEST_ASSERT_EQUAL(WD_ASIC_GIVE_UP, run_asic(&wd, &now, 150000, &in));
}

TEST_CASE("A re-init that brings results back is not escalated", "[mining-watchdog]")
{
    wd_asic_t wd;
    uint64_t now = START_MS;
    wd_asic_init(&wd, now, 0, WD_ASIC_NONE);
    wd_asic_input_t in = {.running = true, .has_work = true, .results = 0, .window_ms = 150000};
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, &now, 150000, &in));
    in.results++;
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, &now, 151000, &in));
}

TEST_CASE("Budget comes back after an hour without recoveries", "[mining-watchdog]")
{
    wd_asic_t wd;
    uint64_t now = START_MS;
    wd_asic_init(&wd, now, WD_ASIC_MAX_RECOVERIES, WD_ASIC_REINIT);
    wd_asic_input_t in = {.running = true, .has_work = true, .results = 0, .window_ms = 150000};
    TEST_ASSERT_EQUAL(WD_ASIC_GIVE_UP, run_asic(&wd, &now, 150000, &in));

    // An hour of healthy mining
    for (int i = 0; i < 3600; i++) {
        now += 1000;
        in.results++;
        wd_asic_step(&wd, now, &in);
    }
    TEST_ASSERT_EQUAL_UINT8(0, wd.recoveries);
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, &now, 150000, &in));
}

TEST_CASE("No work, pause or ramp don't count as silence", "[mining-watchdog]")
{
    wd_asic_t wd;
    uint64_t now = START_MS;
    wd_asic_init(&wd, now, 0, WD_ASIC_NONE);
    wd_asic_input_t in = {.running = true, .has_work = true, .results = 0, .window_ms = 150000};

    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, &now, 100000, &in));
    in.has_work = false;   // held, not reset
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, &now, 600000, &in));
    in.has_work = true;
    TEST_ASSERT_EQUAL(WD_ASIC_REINIT, run_asic(&wd, &now, 50000, &in));

    in.running = false;    // reset
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, &now, 600000, &in));
    in.running = true;
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, run_asic(&wd, &now, 148000, &in));

    // A long gap between ticks (overheat cool-down) counts as one second
    now += 10 * 60 * 1000;
    TEST_ASSERT_EQUAL(WD_ASIC_NONE, wd_asic_step(&wd, now, &in));
}

TEST_CASE("Pool without new work triggers after five minutes", "[mining-watchdog]")
{
    wd_pool_t wd;
    uint64_t now = START_MS;
    wd_pool_init(&wd);
    wd_pool_step(&wd, now, true, 10, 0, 0);

    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, WD_POOL_NO_WORK_MS - 1000, 10, 0, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NO_WORK, run_pool(&wd, &now, 1000, 10, 0, 0));

    // Work every 30 s keeps it quiet
    uint64_t work = 10;
    for (int i = 0; i < 20; i++) {
        work++;
        TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 30000, work, 0, 0));
    }
}

TEST_CASE("Shares without any reply trigger after three minutes", "[mining-watchdog]")
{
    wd_pool_t wd;
    uint64_t now = START_MS;
    uint64_t work = 1;
    wd_pool_init(&wd);
    wd_pool_step(&wd, now, true, work, 0, 0);

    // Submitted 3, answered 3: fine
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 1000, ++work, 3, 3));
    // Two more go out, nothing comes back
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 1000, ++work, 5, 3));
    uint64_t start = now;
    wd_pool_action_t a = WD_POOL_NONE;
    while (a == WD_POOL_NONE && now - start < 10 * 60 * 1000) {
        now += 1000;
        if ((now / 1000) % 30 == 0) work++;
        a = wd_pool_step(&wd, now, true, work, 5, 3);
    }
    TEST_ASSERT_EQUAL(WD_POOL_NO_REPLIES, a);
    TEST_ASSERT_UINT32_WITHIN(1000, WD_POOL_NO_REPLY_MS, (uint32_t) (now - start));
}

TEST_CASE("Late replies and new sessions clear the reply timer", "[mining-watchdog]")
{
    wd_pool_t wd;
    uint64_t now = START_MS;
    wd_pool_init(&wd);
    wd_pool_step(&wd, now, true, 1, 0, 0);

    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 1000, 2, 2, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 120000, 6, 2, 0));
    // A batched reply arrives
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 120000, 10, 2, 2));

    // Session reset: counters start over at 0
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 1000, 11, 4, 2));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 1000, 12, 0, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, 170000, 18, 0, 0));
}

TEST_CASE("Inactive pool watchdog never triggers", "[mining-watchdog]")
{
    wd_pool_t wd;
    uint64_t now = START_MS;
    wd_pool_init(&wd);
    for (int i = 0; i < 3600; i++) {
        now += 1000;
        TEST_ASSERT_EQUAL(WD_POOL_NONE, wd_pool_step(&wd, now, false, 0, 9, 0));
    }
    // Becoming active starts the timers from now
    TEST_ASSERT_EQUAL(WD_POOL_NONE, wd_pool_step(&wd, now, true, 0, 9, 0));
    TEST_ASSERT_EQUAL(WD_POOL_NONE, run_pool(&wd, &now, WD_POOL_NO_WORK_MS - 1000, 0, 9, 0));
}
