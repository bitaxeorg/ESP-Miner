#include "bonanza_power_policy.h"
#include "unity.h"
#include <math.h>
#include <string.h>

typedef struct {
    char calls[96];
    unsigned count;
    bool fail_start, fail_stop, fail_apply, fail_maintenance, fail_save;
    bool cancel_start;
    bool hot_start;
    bool cancelled;
    bonanza_power_target_t saved;
} board_fixture_t;
static void record(board_fixture_t *b, char call) { b->calls[b->count++] = call; }
static bonanza_power_start_result_t start(void *context)
{
    board_fixture_t *b = context; record(b, 'S');
    if (b->cancel_start) b->cancelled = true;
    return b->hot_start ? BONANZA_POWER_START_OVERHEAT :
        b->fail_start ? BONANZA_POWER_START_FAILED : BONANZA_POWER_START_OK;
}
static bool stop(void *context) { board_fixture_t *b = context; record(b, 'X'); return !b->fail_stop; }
static bool apply(void *context, bonanza_power_target_t target)
{
    board_fixture_t *b = context; (void)target; record(b, 'A'); return !b->fail_apply;
}
static bool maintenance(void *context, bonanza_power_owner_t owner, bool acquire)
{
    board_fixture_t *b = context; (void)owner; record(b, acquire ? 'M' : 'R'); return !b->fail_maintenance;
}
static void overheat(void *context, bool enabled) { record(context, enabled ? 'H' : 'C'); }
static bool save(void *context, bonanza_power_target_t target)
{
    board_fixture_t *b = context; record(b, 'V'); b->saved = target; return !b->fail_save;
}
static bool cancelled(void *context) { return ((board_fixture_t *)context)->cancelled; }
static bonanza_power_policy_t make_policy(board_fixture_t *b)
{
    bonanza_power_policy_t policy;
    TEST_ASSERT_TRUE(bonanza_power_policy_init(&policy, (bonanza_power_operations_t){
        .context = b, .start = start, .stop = stop, .apply = apply,
        .maintenance = maintenance, .overheat = overheat, .save_target = save,
        .cancelled = cancelled, .minimum_voltage_mv = 2100, .minimum_frequency_mhz = 800,
    }));
    return policy;
}
static const bonanza_power_target_t TARGET = {.voltage_mv = 2800, .frequency_mhz = 1200};
static const bonanza_power_sample_t COOL = {.health = BONANZA_POWER_HEALTH_OK, .vreg_valid = true, .vreg_c = 70};
static void tick(bonanza_power_policy_t *p, uint64_t now) { bonanza_power_policy_step(p, now, TARGET, COOL, false, false, false); }

TEST_CASE("Power boot waits for the complete mining stack", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b);
    tick(&p, 0); tick(&p, 100);
    TEST_ASSERT_EQUAL_STRING("X", b.calls);
    p.ready = true; tick(&p, 200);
    TEST_ASSERT_EQUAL_STRING("XSA", b.calls);
    TEST_ASSERT_TRUE(p.running);
}
TEST_CASE("Pool recovery cannot override an explicit user pause", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    tick(&p, 0);
    bonanza_power_policy_step(&p, 100, TARGET, COOL, true, false, false);
    TEST_ASSERT_FALSE(p.running); TEST_ASSERT_FALSE(p.paused);
    TEST_ASSERT_TRUE(bonanza_power_policy_pause(&p));
    tick(&p, 200); TEST_ASSERT_FALSE(p.running);
    TEST_ASSERT_EQUAL_STRING("SAXX", b.calls);
    TEST_ASSERT_TRUE(bonanza_power_policy_resume(&p)); TEST_ASSERT_TRUE(p.running);
}
TEST_CASE("Pool recovery repeats startup without setting user pause", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    tick(&p, 0);
    bonanza_power_policy_step(&p, 100, TARGET, COOL, true, false, false);
    bonanza_power_policy_step(&p, 200, TARGET, COOL, true, false, false);
    tick(&p, 300);
    TEST_ASSERT_EQUAL_STRING("SAXSA", b.calls);
}
TEST_CASE("Failed startup and failed tuning cannot report running", "[power-management]")
{
    for (int fail_start = 0; fail_start <= 1; ++fail_start) {
        board_fixture_t b = {.fail_start = fail_start, .fail_apply = !fail_start};
        bonanza_power_policy_t p = make_policy(&b); p.ready = true; tick(&p, 0);
        TEST_ASSERT_TRUE(p.fault); TEST_ASSERT_FALSE(p.running); TEST_ASSERT_TRUE(p.stopped);
        unsigned calls = b.count; tick(&p, 100);
        TEST_ASSERT_EQUAL_UINT(calls, b.count);
        TEST_ASSERT_FALSE(bonanza_power_policy_resume(&p));
    }
}
TEST_CASE("Failed shutdown stays unverified and cannot resume", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true; tick(&p, 0);
    b.fail_stop = true;
    TEST_ASSERT_FALSE(bonanza_power_policy_pause(&p));
    TEST_ASSERT_FALSE(p.stopped); TEST_ASSERT_FALSE(p.running); TEST_ASSERT_TRUE(p.fault);
    TEST_ASSERT_FALSE(bonanza_power_policy_resume(&p));
}
TEST_CASE("A stop arriving during startup cancels without a spurious hardware fault", "[power-management]")
{
    board_fixture_t b = {.cancel_start = true}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    tick(&p, 0);
    TEST_ASSERT_EQUAL_STRING("SX", b.calls);
    TEST_ASSERT_FALSE(p.running); TEST_ASSERT_FALSE(p.fault); TEST_ASSERT_TRUE(p.stopped);
    TEST_ASSERT_TRUE(bonanza_power_policy_pause(&p));
    b.cancel_start = false; b.cancelled = false;
    TEST_ASSERT_TRUE(bonanza_power_policy_resume(&p));
}
TEST_CASE("Overheat needs valid cooldown evidence and reduces settings once", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    tick(&p, 0);
    bonanza_power_sample_t hot = COOL; hot.health = BONANZA_POWER_HEALTH_OVERHEAT;
    bonanza_power_policy_step(&p, 100, TARGET, hot, false, false, false);
    TEST_ASSERT_TRUE(p.cooling); TEST_ASSERT_FALSE(p.running);
    bonanza_power_sample_t missing = COOL; missing.vreg_valid = false;
    bonanza_power_policy_step(&p, 40000, TARGET, missing, false, false, true);
    TEST_ASSERT_EQUAL_STRING("SAHX", b.calls);
    tick(&p, 30100);
    TEST_ASSERT_EQUAL_STRING("SAHXVSC", b.calls);
    TEST_ASSERT_EQUAL_UINT16(2700, b.saved.voltage_mv);
    TEST_ASSERT_EQUAL_FLOAT(1100, b.saved.frequency_mhz);
    TEST_ASSERT_FALSE(p.cooling); TEST_ASSERT_TRUE(p.running);
}
TEST_CASE("Cooling remains interruptible by pause pool loss and maintenance", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    bonanza_power_policy_step(&p, 0, TARGET, COOL, false, false, true);
    TEST_ASSERT_TRUE(bonanza_power_policy_pause(&p));
    tick(&p, 30000);
    TEST_ASSERT_TRUE(p.cooling); TEST_ASSERT_FALSE(p.running);
    TEST_ASSERT_TRUE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, true));
    unsigned count = b.count; tick(&p, 60000); TEST_ASSERT_EQUAL_UINT(count, b.count);
    TEST_ASSERT_TRUE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, false));
    p.pool_unavailable = true;
    TEST_ASSERT_TRUE(bonanza_power_policy_resume(&p));
    bonanza_power_policy_step(&p, 61000, TARGET, COOL, true, false, true);
    TEST_ASSERT_FALSE(p.running);
    tick(&p, 62000); TEST_ASSERT_TRUE(p.running);
    TEST_ASSERT_EQUAL_UINT16(2700, b.saved.voltage_mv);
}
TEST_CASE("Maintenance is exclusive and release does not auto resume", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true; tick(&p, 0);
    TEST_ASSERT_TRUE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_RESTART, true));
    TEST_ASSERT_FALSE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, true));
    TEST_ASSERT_FALSE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, false));
    TEST_ASSERT_FALSE(bonanza_power_policy_resume(&p));
    unsigned count = b.count; tick(&p, 100); TEST_ASSERT_EQUAL_UINT(count, b.count);
    TEST_ASSERT_TRUE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_RESTART, false));
    tick(&p, 200); TEST_ASSERT_FALSE(p.running);
    TEST_ASSERT_TRUE(bonanza_power_policy_resume(&p));
}
TEST_CASE("Failed maintenance never grants update ownership", "[power-management]")
{
    board_fixture_t b = {.fail_maintenance = true}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    TEST_ASSERT_FALSE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, true));
    TEST_ASSERT_EQUAL(BONANZA_POWER_OWNER_NONE, p.owner); TEST_ASSERT_TRUE(p.fault);
    TEST_ASSERT_FALSE(p.stopped); TEST_ASSERT_FALSE(p.running);
}

TEST_CASE("Overheat reduction clamps to board limits and save failures stay off", "[power-management]")
{
    for (int fail = 0; fail <= 1; ++fail) {
        board_fixture_t b = {.fail_save = fail}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
        bonanza_power_target_t minimum = {.voltage_mv = 2100, .frequency_mhz = 800};
        bonanza_power_policy_step(&p, 0, minimum, COOL, false, false, true);
        bonanza_power_policy_step(&p, 30000, minimum, COOL, false, false, true);
        TEST_ASSERT_EQUAL_UINT16(2100, b.saved.voltage_mv);
        TEST_ASSERT_EQUAL_FLOAT(800, b.saved.frequency_mhz);
        TEST_ASSERT_EQUAL(fail, p.fault); TEST_ASSERT_EQUAL(!fail, p.running);
    }
}
TEST_CASE("Invalid required power operations are rejected", "[power-management]")
{
    bonanza_power_policy_t p;
    TEST_ASSERT_FALSE(bonanza_power_policy_init(NULL, (bonanza_power_operations_t){0}));
    TEST_ASSERT_FALSE(bonanza_power_policy_init(&p, (bonanza_power_operations_t){0}));
}

TEST_CASE("Overheat discovered during startup enters the shared cooling path", "[power-management]")
{
    board_fixture_t b = {.hot_start = true}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    tick(&p, 0);
    TEST_ASSERT_FALSE(p.running); TEST_ASSERT_TRUE(p.cooling); TEST_ASSERT_FALSE(p.fault);
    TEST_ASSERT_EQUAL_STRING("SHX", b.calls);
    b.hot_start = false;
    tick(&p, 100); tick(&p, 30099); TEST_ASSERT_FALSE(p.running);
    tick(&p, 30100); TEST_ASSERT_TRUE(p.running); TEST_ASSERT_FALSE(p.cooling);
    TEST_ASSERT_EQUAL_STRING("SHXVSC", b.calls);
}
TEST_CASE("Failed maintenance release returns to a fault without orphaned ownership", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    TEST_ASSERT_TRUE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, true));
    b.fail_maintenance = true;
    TEST_ASSERT_FALSE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_OTA, false));
    TEST_ASSERT_EQUAL(BONANZA_POWER_OWNER_NONE, p.owner);
    TEST_ASSERT_TRUE(p.fault); TEST_ASSERT_FALSE(p.stopped);
    tick(&p, 100); TEST_ASSERT_TRUE(p.stopped); TEST_ASSERT_FALSE(p.running);
}
TEST_CASE("Cooling rejects invalid clocks and hot regulators", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    bonanza_power_policy_step(&p, 100, TARGET, COOL, false, false, true);
    bonanza_power_sample_t sample = COOL;
    sample.vreg_c = NAN;
    bonanza_power_policy_step(&p, 30100, TARGET, sample, false, false, true);
    sample.vreg_c = 100;
    bonanza_power_policy_step(&p, 30100, TARGET, sample, false, false, true);
    TEST_ASSERT_FALSE(p.running);
    tick(&p, 99); TEST_ASSERT_FALSE(p.running);
    tick(&p, 30100); TEST_ASSERT_TRUE(p.running);
}
TEST_CASE("Shared cooling preserves a BM1373 target below 400 MHz", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    p.operations.minimum_voltage_mv = 1000; p.operations.minimum_frequency_mhz = 50;
    bonanza_power_target_t target = {.voltage_mv = 1000, .frequency_mhz = 327};
    bonanza_power_policy_step(&p, 0, target, COOL, false, false, true);
    bonanza_power_policy_step(&p, 30000, target, COOL, false, false, true);
    TEST_ASSERT_EQUAL_UINT16(1000, b.saved.voltage_mv);
    TEST_ASSERT_EQUAL_FLOAT(227, b.saved.frequency_mhz);
}

TEST_CASE("A still-hot restart begins a fresh cooling period", "[power-management]")
{
    board_fixture_t b = {.hot_start = true}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    bonanza_power_policy_step(&p, 0, TARGET, COOL, false, false, true);
    tick(&p, 30000);
    TEST_ASSERT_TRUE(p.cooling); TEST_ASSERT_FALSE(p.fault); TEST_ASSERT_FALSE(p.running);
    b.hot_start = false;
    tick(&p, 59999); TEST_ASSERT_FALSE(p.running);
    tick(&p, 60000); TEST_ASSERT_TRUE(p.running);
}

TEST_CASE("Fatal board health retries an unverified shutdown without automatic restart", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b); p.ready = true;
    tick(&p, 0);
    bonanza_power_sample_t bad = COOL; bad.health = BONANZA_POWER_HEALTH_FAULT;
    b.fail_stop = true;
    bonanza_power_policy_step(&p, 100, TARGET, bad, false, false, false);
    TEST_ASSERT_TRUE(p.fault); TEST_ASSERT_FALSE(p.stopped);
    b.fail_stop = false;
    tick(&p, 200);
    TEST_ASSERT_TRUE(p.stopped); TEST_ASSERT_FALSE(p.running);
    TEST_ASSERT_FALSE(bonanza_power_policy_resume(&p));
}

TEST_CASE("Unknown maintenance owners cannot change board ownership", "[power-management]")
{
    board_fixture_t b = {0}; bonanza_power_policy_t p = make_policy(&b);
    TEST_ASSERT_FALSE(bonanza_power_policy_maintenance(&p, BONANZA_POWER_OWNER_NONE, true));
    TEST_ASSERT_FALSE(bonanza_power_policy_maintenance(&p, (bonanza_power_owner_t)99, true));
    TEST_ASSERT_EQUAL_UINT(0, b.count);
}
