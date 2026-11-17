#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "global_state.h"
#include "safety_monitor.h"

static const char * TAG = "safety";

#define STARTUP_GRACE_US (30 * 1000000LL)   // after the chips come up: ramp, first counter reads
#define SENSOR_LOST_US   (3 * 1000000LL)
#define FAN_STALL_US     (10 * 1000000LL)
#define ASIC_STOPPED_US  (15 * 1000000LL)
#define VCORE_LOST_US    (3 * 1000000LL)

#define FAN_SPINNING_RPM 300
#define FAN_MIN_DUTY     30.0f   // below this duty a slow fan is expected
#define ASIC_MIN_SHARE   0.10f   // of the best other chip's hashrate
#define VCORE_MIN_SHARE  0.5f    // of the core voltage set

#define MAX_ASICS 8

typedef struct {
    int64_t since_us;   // when the condition started, 0 while it doesn't hold
} condition_t;

static int64_t asic_up_since_us = 0;
static bool fan_seen_spinning[2] = {false, false};
static condition_t chip_temp_lost, fan_stall[2], vcore_lost, no_hashing, asic_stopped[MAX_ASICS];

static bool held(condition_t * c, bool active, int64_t now_us, int64_t limit_us)
{
    if (!active) {
        c->since_us = 0;
        return false;
    }
    if (c->since_us == 0) {
        c->since_us = now_us;
    }
    return now_us - c->since_us >= limit_us;
}

static void reset_all(void)
{
    chip_temp_lost.since_us = 0;
    fan_stall[0].since_us = fan_stall[1].since_us = 0;
    vcore_lost.since_us = 0;
    no_hashing.since_us = 0;
    for (int i = 0; i < MAX_ASICS; i++) {
        asic_stopped[i].since_us = 0;
    }
}

static void __attribute__((format(printf, 2, 3))) fault(GlobalState * GLOBAL_STATE, const char * fmt, ...)
{
    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;
    va_list args;
    va_start(args, fmt);
    vsnprintf(sys->hardware_fault_msg, sizeof(sys->hardware_fault_msg), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s, cutting power", sys->hardware_fault_msg);
    sys->hardware_fault = true;
}

static void check_fan(GlobalState * GLOBAL_STATE, int fan, uint16_t rpm, int64_t now_us)
{
    // Only a fan that has been seen spinning: a board may have an empty header
    if (rpm >= FAN_SPINNING_RPM) {
        fan_seen_spinning[fan] = true;
    }
    bool stalled = fan_seen_spinning[fan] && rpm < FAN_SPINNING_RPM
                && GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc >= FAN_MIN_DUTY;
    if (held(&fan_stall[fan], stalled, now_us, FAN_STALL_US)) {
        fault(GLOBAL_STATE, "Fan %d stalled", fan + 1);
    }
}

static void check_asics(GlobalState * GLOBAL_STATE, int64_t now_us)
{
    HashrateMonitorModule * monitor = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    if (!monitor->is_initialized || asic_count > MAX_ASICS) {
        return;
    }

    float hashrate[MAX_ASICS];
    bool fresh[MAX_ASICS];
    bool reported[MAX_ASICS];
    pthread_mutex_lock(&monitor->lock);
    for (int i = 0; i < asic_count; i++) {
        uint64_t t = monitor->total_measurement[i].time_us;
        hashrate[i] = monitor->total_measurement[i].hashrate;
        // Chips without hash counters (BM1397) never set time_us; for the others
        // a counter that stops updating means the chip stopped answering
        reported[i] = t != 0;
        fresh[i] = !reported[i] || now_us - (int64_t) t < ASIC_STOPPED_US;
    }
    pthread_mutex_unlock(&monitor->lock);

    // Nothing comes back from the chain while work is going out
    bool any_alive = false;
    for (int i = 0; i < asic_count; i++) {
        if (fresh[i] && hashrate[i] > 0.0f) {
            any_alive = true;
        }
    }
    bool working = GLOBAL_STATE->has_work && !GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable;
    if (held(&no_hashing, working && !any_alive, now_us, ASIC_STOPPED_US)) {
        fault(GLOBAL_STATE, "No chip is hashing");
        return;
    }

    if (asic_count < 2) {
        return;
    }
    // One chip far behind the others. On boards with chips in series voltage
    // domains, a chip that stops drawing current lets its domain's voltage climb.
    for (int i = 0; i < asic_count; i++) {
        float best_other = 0.0f;
        for (int j = 0; j < asic_count; j++) {
            if (j != i && fresh[j] && hashrate[j] > best_other) {
                best_other = hashrate[j];
            }
        }
        bool stopped = reported[i] && best_other > 0.0f
                    && (!fresh[i] || hashrate[i] < ASIC_MIN_SHARE * best_other);
        if (held(&asic_stopped[i], stopped, now_us, ASIC_STOPPED_US)) {
            fault(GLOBAL_STATE, "ASIC %d stopped hashing", i + 1);
            return;
        }
    }
}

void SAFETY_check(GlobalState * GLOBAL_STATE, uint16_t core_voltage_target_mv)
{
    PowerManagementModule * power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    int64_t now_us = esp_timer_get_time();

    if (!GLOBAL_STATE->ASIC_initalized || GLOBAL_STATE->SELF_TEST_MODULE.is_active || GLOBAL_STATE->SYSTEM_MODULE.hardware_fault) {
        asic_up_since_us = 0;
        reset_all();
        return;
    }
    if (asic_up_since_us == 0) {
        asic_up_since_us = now_us;
    }

    check_fan(GLOBAL_STATE, 0, power->fan_rpm, now_us);
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2302) {
        check_fan(GLOBAL_STATE, 1, power->fan2_rpm, now_us);
    }

    // The sensor drivers return -1 when a read fails: never take that as cool
    if (held(&chip_temp_lost, power->chip_temp_avg == -1.0f, now_us, SENSOR_LOST_US)) {
        fault(GLOBAL_STATE, "Temperature sensor lost");
        return;
    }

    if (now_us - asic_up_since_us < STARTUP_GRACE_US) {
        return;
    }

    bool vcore_low = core_voltage_target_mv > 0 && power->core_voltage < VCORE_MIN_SHARE * core_voltage_target_mv;
    if (held(&vcore_lost, vcore_low, now_us, VCORE_LOST_US)) {
        fault(GLOBAL_STATE, "Core voltage lost (%.0f mV of %u mV)", power->core_voltage, core_voltage_target_mv);
        return;
    }

    check_asics(GLOBAL_STATE, now_us);
}
