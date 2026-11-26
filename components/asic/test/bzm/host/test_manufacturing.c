#include "check.h"
#include "bzm/bringup.h"
#include "bzm/frequency.h"
#include "bzm/power.h"
#include "bzm/registers.h"
#include <math.h>
#include <string.h>

typedef struct {
    uint32_t registers[BZM_MAX_ASIC_COUNT][256];
    bool assigned[BZM_MAX_ASIC_COUNT];
    unsigned unaddressed, writes, pairs, batches, barriers;
    uint64_t now;
    bool missing_telemetry, unlocked, fail_pair;
} chain_t;

static bzm_bringup_probe_result_t probe(void *context, uint8_t id)
{
    chain_t *c = context;
    size_t index;
    bool present = id == BZM_BROADCAST_ASIC ? c->unaddressed != 0 :
        bzm_topology_asic_index(id, &index) && c->assigned[index];
    return present ? BZM_BRINGUP_PROBE_RESPONSE : BZM_BRINGUP_PROBE_NO_RESPONSE;
}
static bool write_register(void *context, uint8_t id, uint16_t engine, uint8_t offset, uint32_t value)
{
    chain_t *c = context;
    CHECK(engine == BZM_BRINGUP_CONTROL_ENGINE_ID);
    ++c->writes;
    size_t index;
    if (id == BZM_BROADCAST_ASIC && offset == BZM_LOCAL_REG_ASIC_ID) {
        CHECK(c->unaddressed > 0 && bzm_topology_asic_index(value & 0xff, &index));
        --c->unaddressed;
        c->assigned[index] = true;
        c->registers[index][offset] = value | 0x100;
    } else if (id == BZM_ALL_ASICS) {
        for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) c->registers[i][offset] = value;
    } else {
        CHECK(bzm_topology_asic_index(id, &index));
        c->registers[index][offset] = value;
    }
    return true;
}
static bool read_register(void *context, uint8_t id, uint16_t engine, uint8_t offset, uint32_t *value)
{
    chain_t *c = context;
    CHECK(engine == BZM_BRINGUP_CONTROL_ENGINE_ID);
    size_t index;
    CHECK(bzm_topology_asic_index(id, &index));
    *value = c->registers[index][offset];
    if (!c->unlocked && (offset == BZM_LOCAL_REG_PLL0_ENABLE || offset == BZM_LOCAL_REG_PLL1_ENABLE)) *value |= 4;
    return true;
}
static void delay(void *context, uint32_t ms) { ((chain_t *)context)->now += (uint64_t)ms * 1000; }
static uint64_t now(void *context) { return ((chain_t *)context)->now; }
static bool telemetry(void *context, bzm_telemetry_store_t *store, uint16_t timeout_ms)
{
    chain_t *c = context;
    (void)timeout_ms;
    bzm_telemetry_store_init(store);
    for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) {
        store->samples[i] = (bzm_telemetry_sample_t){
            .asic_id = bzm_asic_wire_ids[i], .timestamp_us = c->now,
            .received = !(c->missing_telemetry && i == 2),
            .temperature_c = 50, .thermal_enabled = true, .thermal_validity = true, .thermal_valid = true,
            .ch0_mv = 350, .ch1_mv = 350, .voltage_enabled = true, .voltage_valid = true,
            .pll_locked = !c->unlocked, .valid = true,
        };
    }
    return true;
}
static bool batch_begin(void *context, uint16_t pair)
{
    chain_t *c = context;
    CHECK(pair == c->batches);
    ++c->batches;
    return true;
}
static bool pair_commit(void *context, uint8_t id, const bzm_engine_pair_t *pair)
{
    chain_t *c = context;
    CHECK(id == bzm_asic_wire_ids[c->pairs % BZM_MAX_ASIC_COUNT]);
    CHECK(pair->pair_index == c->pairs / BZM_MAX_ASIC_COUNT);
    CHECK(pair->bottom.stack == BZM_ENGINE_STACK_BOTTOM && pair->top.stack == BZM_ENGINE_STACK_TOP);
    if (c->fail_pair && c->pairs == 5) return false;
    ++c->pairs;
    return true;
}
static bool batch_end(void *context, uint16_t pair)
{
    chain_t *c = context;
    CHECK(c->pairs == (pair + 1U) * BZM_MAX_ASIC_COUNT);
    return true;
}
static bool barrier(void *context) { ++((chain_t *)context)->barriers; return true; }

static const bzm_bringup_ops_t OPS = {
    .probe_noop = probe, .write_u32 = write_register, .read_u32 = read_register,
    .delay_ms = delay, .now_us = now, .telemetry_snapshot = telemetry,
    .balanced_batch_begin = batch_begin, .balanced_pair_commit = pair_commit,
    .balanced_batch_end = batch_end, .activation_barrier = barrier,
};
static const bzm_bringup_telemetry_policy_t POLICY = {
    .bounds = {.temperature_min_c = -20, .temperature_max_c = 105,
               .ch0_min_mv = 250, .ch0_max_mv = 550, .ch1_min_mv = 250, .ch1_max_mv = 550,
               .ch2_abs_max_mv = 50, .max_stack_spread_mv = 100},
    .max_age_us = 1000000, .ch2_confirm_samples = 3,
};

static chain_t chain(void)
{
    chain_t c = {.unaddressed = BZM_MAX_ASIC_COUNT, .now = 1000000};
    for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) {
        c.registers[i][BZM_LOCAL_REG_BANDGAP] = 0xabcd1234;
        c.registers[i][BZM_LOCAL_REG_UART_TDM_CONTROL] = 0xfec9;
        c.registers[i][BZM_LOCAL_REG_SLOW_CLOCK_DIVIDER] = 2;
        c.registers[i][BZM_LOCAL_REG_TDM_DELAY] = 1;
        c.registers[i][BZM_LOCAL_REG_UART_TX] = 0x0f;
        c.registers[i][BZM_LOCAL_REG_SENSOR_TDM_GAP_COUNT] = 63;
        c.registers[i][BZM_LOCAL_REG_SENSOR_CLOCK_DIVIDER] = 0x108;
    }
    return c;
}

static void test_startup(void)
{
    chain_t c = chain();
    bzm_bringup_state_t state;
    bzm_bringup_report_t report;
    CHECK(bzm_bringup_start(&state, &OPS, &c, &POLICY, &report) == BZM_BRINGUP_GOOD);
    CHECK(state.running && state.clock_mhz == 800);
    CHECK(c.unaddressed == 0 && c.pairs == 472 && c.batches == 118 && c.barriers == 1);
    CHECK(c.writes == 112 && c.now == 1814000);
    CHECK(report.reason == BZM_BRINGUP_REASON_NONE);
    for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) {
        CHECK(state.domain_clock_mhz[i][0] == 800 && state.domain_clock_mhz[i][1] == 800);
    }
}

static void test_startup_failures(void)
{
    const bzm_bringup_reason_t reasons[] = {
        BZM_BRINGUP_REASON_CHAIN_MISSING, BZM_BRINGUP_REASON_TELEMETRY_MISSING,
        BZM_BRINGUP_REASON_PLL_UNLOCKED, BZM_BRINGUP_REASON_BALANCED_PAIR_COMMIT,
    };
    for (unsigned fault = 0; fault < 4; ++fault) {
        chain_t c = chain();
        if (fault == 0) c.unaddressed = 0;
        if (fault == 1) c.missing_telemetry = true;
        if (fault == 2) c.unlocked = true;
        if (fault == 3) c.fail_pair = true;
        bzm_bringup_state_t state;
        bzm_bringup_report_t report;
        CHECK(bzm_bringup_start(&state, &OPS, &c, &POLICY, &report) == BZM_BRINGUP_BAD);
        CHECK(!state.running && c.barriers == 0);
        CHECK(report.reason == reasons[fault]);
    }
}

typedef struct { bool five_volt, enabled, fail; float volts; unsigned delays; } power_t;
static bzm_status_t set_5v(void *context, bool enabled) { ((power_t *)context)->five_volt = enabled; return BZM_OK; }
static bzm_status_t enable(void *context, bool enabled) { ((power_t *)context)->enabled = enabled; return BZM_OK; }
static bzm_status_t voltage(void *context, float volts) { ((power_t *)context)->volts = volts; return BZM_OK; }
static bzm_status_t validate(void *context, float volts)
{
    power_t *p = context;
    CHECK(p->enabled && p->volts == volts);
    return p->fail ? 77 : BZM_OK;
}
static void power_delay(void *context, uint32_t ms) { CHECK(ms == 100); ++((power_t *)context)->delays; }
static void test_power(void)
{
    const bzm_power_ops_t ops = {set_5v, enable, voltage, validate, power_delay};
    power_t p = {0};
    CHECK(bzm_power_set_rail_enabled(&ops, &p, true) == BZM_OK);
    CHECK(p.enabled && !p.five_volt && p.volts == 2.8f && p.delays == 2);
    CHECK(bzm_power_set_runtime_voltage(&ops, &p, NAN) == BZM_ERR_INVALID_ARG);
    CHECK(p.volts == 2.8f);
    p.fail = true;
    CHECK(bzm_power_set_runtime_voltage(&ops, &p, 3.0f) == 77);
    CHECK(!p.enabled && !p.five_volt && p.volts == 0);
    CHECK(bzm_power_set_rail_enabled(&ops, &p, true) == 77);
    CHECK(!p.enabled && p.volts == 0);
}

int main(void)
{
    test_startup();
    test_startup_failures();
    test_power();
    puts("BZM host manufacturing: startup, four fault cases, and power sequencing passed");
    return 0;
}
