#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "thermal.h"
#include "power.h"
#include "asic.h"
#include "asic_common.h"
#include "utils.h"
#include "asic_init.h"
#include "asic_reset.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <stdio.h>

#define POLL_RATE 100
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define SAFE_TEMP 45.0

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

// Physically plausible temperature envelope shared by the VR and ASIC
// readings. A value outside it (e.g. a corrupted PMBus read yielding 16384 C)
// is a sensor fault, not heat, and must never be acted upon.
#define TEMP_PLAUSIBLE_MIN (-40.0)
#define TEMP_PLAUSIBLE_MAX 150.0

// Consecutive polls with a plausible over-limit reading required before a
// permanent downgrade is written to NVS. One spurious sample is not evidence.
#define OVERHEAT_CONSECUTIVE_READINGS 2

#define ASIC_REDUCTION 100.0

static const char * TAG = "power_management";

static bool temp_is_plausible(float temp)
{
    return temp >= TEMP_PLAUSIBLE_MIN && temp <= TEMP_PLAUSIBLE_MAX;
}

static void mining_stop(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Stopping mining");

    // Wind frequency down to 50 MHz before cutting power. This also updates
    // the transition tracker so the ramp starts from 50 MHz on next start,
    // rather than the stale pre-reset frequency.
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = 50;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = 0;

    ASIC_set_frequency(GLOBAL_STATE);
    ASIC_set_nonce_space(GLOBAL_STATE);

    // Cut ASIC power and hold in reset
    VCORE_set_voltage(GLOBAL_STATE, 0.0f);
    asic_hold_reset_low();

    // Mark uninitialized immediately so tasks stop issuing UART commands
    GLOBAL_STATE->ASIC_initalized = false;

    // Give tasks time to complete any in-progress UART operation
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Flush any stale data from the UART buffers
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Mining stopped");
}

static uint8_t mining_start(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Starting mining");

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);

    // Restore voltage from NVS
    uint16_t voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
    VCORE_set_voltage(GLOBAL_STATE, (double) voltage / 1000.0);

    // Wait for voltage to stabilize before touching the ASIC
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Clear any accumulated UART garbage before init
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Stabilization delay of 2000ms prevents race conditions where tasks are
    // just starting to use the ASIC while power management tries to change frequency
    uint8_t chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, 2000);

    if (chip_count > 0) {
        ESP_LOGI(TAG, "Mining started successfully (%d chip(s))", chip_count);
    } else {
        ESP_LOGE(TAG, "Mining start failed - ASIC not detected");
    }

    return chip_count;
}

static float expected_hashrate(GlobalState * GLOBAL_STATE)
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0;
}

float POWER_MANAGEMENT_get_nominal_hashrate(const GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate > 0.0f) {
        return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate;
    }

    float freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    if (freq <= 0.0f) {
        freq = (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_frequency_mhz;
    }

    return (freq * (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic_count) / 1000.0f;
}

void POWER_MANAGEMENT_init_frequency(GlobalState * GLOBAL_STATE)
{
    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = frequency;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = 50.0;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = expected_hashrate(GLOBAL_STATE);
    
    char expected_hashrate_str[16] = {0};
    suffixString(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate * 1e6, expected_hashrate_str, sizeof(expected_hashrate_str), 0);
    ESP_LOGI(TAG, "ASIC Frequency: %g MHz, Expected hashrate: %sH/s", frequency, expected_hashrate_str);
}

// --- Mining watchdog (issue #1053, "flatline of death") ---------------------
// A chain that keeps drawing power but returns no results freezes the hashrate
// averages, and nothing notices: the miner looks alive while it is doing no
// work. Two independent signals, both conservative, trigger a chain restart.
#define WATCHDOG_HASHRATE_FLATLINE_S 180
#define WATCHDOG_NO_SHARE_S 300
// Grace period after a (re)start: a cold chain ramps for minutes and must never
// be mistaken for a flatline.
#define WATCHDOG_WARMUP_S 300
#define WATCHDOG_MAX_RECOVERIES_PER_HOUR 3
#define WATCHDOG_MIN_FREQUENCY_MHZ 50.0f
#define WATCHDOG_HOUR_US (3600LL * 1000000LL)

typedef struct
{
    int64_t started_us;   // last mining start, anchors the warm-up grace period
    float last_hashrate_1m;
    int64_t hashrate_since_us;
    uint64_t last_share_count;
    int64_t shares_since_us;
    int64_t recovery_us[WATCHDOG_MAX_RECOVERIES_PER_HOUR];
    int recovery_count;
} mining_watchdog_t;

static uint64_t watchdog_share_count(const SystemModule * sys_module)
{
    uint64_t total = 0;
    for (int i = 0; i < MAX_POOLS; i++) {
        total += sys_module->pool_sessions[i].shares_accepted;
        total += sys_module->pool_sessions[i].shares_rejected;
    }
    return total;
}

// Only a miner that is really mining can flatline. Anything else (paused,
// overheat, hardware fault, self-test, no pool job, chain still at idle
// frequency) keeps the watchdog out of the way.
static bool watchdog_is_eligible(const GlobalState * GLOBAL_STATE, bool is_paused)
{
    const SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;
    const PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    return !is_paused
        && !sys_module->mining_paused
        && !sys_module->overheat_mode
        && !sys_module->hardware_fault
        && !sys_module->pools_unavailable
        && !GLOBAL_STATE->SELF_TEST_MODULE.is_active
        && GLOBAL_STATE->ASIC_initalized
        && GLOBAL_STATE->has_work
        && power_management->frequency_value > WATCHDOG_MIN_FREQUENCY_MHZ
        && power_management->expected_hashrate > 0.0f;
}

static void watchdog_reset_signals(mining_watchdog_t * wd, const SystemModule * sys_module,
                                   int64_t now_us)
{
    wd->last_hashrate_1m = sys_module->hashrate_1m;
    wd->hashrate_since_us = now_us;
    wd->last_share_count = watchdog_share_count(sys_module);
    wd->shares_since_us = now_us;
}

static void watchdog_tick(mining_watchdog_t * wd, GlobalState * GLOBAL_STATE, bool is_paused)
{
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;
    int64_t now_us = esp_timer_get_time();

    if (!watchdog_is_eligible(GLOBAL_STATE, is_paused) ||
        now_us - wd->started_us < (int64_t) WATCHDOG_WARMUP_S * 1000000LL) {
        watchdog_reset_signals(wd, sys_module, now_us);
        return;
    }

    // (a) hashrate_1m stuck at exactly the same value. Zero is not a flatline:
    //     a chain reporting nothing is covered by the share signal instead.
    if (sys_module->hashrate_1m != wd->last_hashrate_1m) {
        wd->last_hashrate_1m = sys_module->hashrate_1m;
        wd->hashrate_since_us = now_us;
    }

    // (b) no share accepted or rejected, pool answers included.
    uint64_t shares = watchdog_share_count(sys_module);
    if (shares != wd->last_share_count) {
        wd->last_share_count = shares;
        wd->shares_since_us = now_us;
    }

    bool hashrate_flat = wd->last_hashrate_1m > 0.0f &&
        now_us - wd->hashrate_since_us >= (int64_t) WATCHDOG_HASHRATE_FLATLINE_S * 1000000LL;
    bool no_shares =
        now_us - wd->shares_since_us >= (int64_t) WATCHDOG_NO_SHARE_S * 1000000LL;

    const char *reason = hashrate_flat ? "hashrate_1m frozen" : (no_shares ? "no new shares" : NULL);
    if (reason == NULL) {
        return;
    }

    // Forget recoveries older than an hour.
    int kept = 0;
    for (int i = 0; i < wd->recovery_count; i++) {
        if (now_us - wd->recovery_us[i] < WATCHDOG_HOUR_US) {
            wd->recovery_us[kept++] = wd->recovery_us[i];
        }
    }
    wd->recovery_count = kept;

    if (wd->recovery_count >= WATCHDOG_MAX_RECOVERIES_PER_HOUR) {
        if (!sys_module->hardware_fault) {
            ESP_LOGE(TAG, "Mining watchdog: %s after %d restarts in an hour, giving up and flagging a hardware fault",
                     reason, wd->recovery_count);
            snprintf(sys_module->hardware_fault_msg, sizeof(sys_module->hardware_fault_msg),
                     "Mining stalled: %s", reason);
            sys_module->hardware_fault = true;
        }
        watchdog_reset_signals(wd, sys_module, now_us);
        return;
    }

    float stalled_s = hashrate_flat
        ? (float) (now_us - wd->hashrate_since_us) / 1e6f
        : (float) (now_us - wd->shares_since_us) / 1e6f;

    ESP_LOGW(TAG, "Mining watchdog: %s for %.0fs (hashrate_1m %.2f GH/s, %llu shares); restarting the ASIC chain (%d/%d this hour)",
             reason, stalled_s, sys_module->hashrate_1m, (unsigned long long) shares,
             wd->recovery_count + 1, WATCHDOG_MAX_RECOVERIES_PER_HOUR);

    wd->recovery_us[wd->recovery_count++] = now_us;

    mining_stop(GLOBAL_STATE);
    mining_start(GLOBAL_STATE);

    wd->started_us = esp_timer_get_time();
    watchdog_reset_signals(wd, sys_module, wd->started_us);
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);
    
    float last_asic_frequency = power_management->frequency_value;

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;

    uint16_t last_known_asic_voltage = 0;
    float last_known_asic_frequency = 0.0;
    bool is_paused = false;
    int overheat_streak = 0;

    mining_watchdog_t watchdog = { .started_us = esp_timer_get_time() };
    watchdog_reset_signals(&watchdog, sys_module, watchdog.started_us);

    while (1) {
        if (GLOBAL_STATE->SELF_TEST_MODULE.is_finished) {
            ESP_LOGI(TAG, "Stopped");
            vTaskDelete(NULL);
            return;
        }

        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        Power_get_output(GLOBAL_STATE, &power_management->power, &power_management->current);
        power_management->core_voltage = VCORE_get_voltage_mv(GLOBAL_STATE);

        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
        power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        // User pause, hardware fault, or all pools unreachable
        bool wants_stop = sys_module->mining_paused || sys_module->hardware_fault || sys_module->pools_unavailable;
        if (wants_stop && !is_paused) {
            mining_stop(GLOBAL_STATE);
            is_paused = true;
        } else if (!wants_stop && is_paused) {
            mining_start(GLOBAL_STATE);
            is_paused = false;
            // A fresh start gets a fresh grace period.
            watchdog.started_us = esp_timer_get_time();
            watchdog_reset_signals(&watchdog, sys_module, watchdog.started_us);
        }

        // If we've paused or have a hardware fault, skip doing anything else
        if (is_paused || sys_module->hardware_fault) {
            overheat_streak = 0; // stale samples must not count as "consecutive"
            vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
            continue;
        }

        bool vr_plausible = temp_is_plausible(power_management->vr_temp);
        bool asic1_plausible = temp_is_plausible(power_management->chip_temp_avg);
        bool asic2_plausible = temp_is_plausible(power_management->chip_temp2_avg);

        if (!vr_plausible) {
            ESP_LOGW(TAG, "Implausible VR temperature %.1fC, not acting on it", power_management->vr_temp);
        }

        // Only plausible, over-limit readings count as heat. Unavailable ASIC
        // sensors report -1 and are simply not warm.
        bool vr_overheat = vr_plausible && power_management->vr_temp > TPS546_THROTTLE_TEMP;
        bool asic_overheat =
            (asic1_plausible && power_management->chip_temp_avg > THROTTLE_TEMP)
            || (asic2_plausible && power_management->chip_temp2_avg > THROTTLE_TEMP);

        overheat_streak = (vr_overheat || asic_overheat) ? overheat_streak + 1 : 0;

        if (overheat_streak >= OVERHEAT_CONSECUTIVE_READINGS && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            overheat_streak = 0;
            if (power_management->chip_temp2_avg > 0) {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC1: %fC ASIC2: %fC", power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
            } else {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC: %fC", power_management->vr_temp, power_management->chip_temp_avg);
            }

            last_known_asic_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
            last_known_asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
            nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, false);
            nvs_config_set_u16(NVS_CONFIG_MANUAL_FAN_SPEED, 100);
            nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, true);
            ESP_LOGW(TAG, "Entering safe mode due to overheat condition. System operation halted.");
            mining_stop(GLOBAL_STATE);
            
            // Note: ASIC temperature readings are invalid when ASIC is powered down (returns -1)
            // For 600-series boards that use ASIC thermal diode, we rely on VR temp and fixed cooling time
            // For boards with EMC internal temp sensor, readings remain valid
            bool asic_temp_valid = GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp;
            int cooling_cycles = 0;
            const int MIN_COOLING_CYCLES = 6; // Minimum 30 seconds cooling
            
            while (cooling_cycles < MIN_COOLING_CYCLES ||
                   (temp_is_plausible(power_management->vr_temp) && power_management->vr_temp > TPS546_THROTTLE_TEMP - 10)) {
                vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait 5 seconds
                cooling_cycles++;
                
                power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
                
                // Only check ASIC temps if they're valid (not using ASIC thermal diode)
                if (asic_temp_valid) {
                    power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
                    power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);
                    ESP_LOGW(TAG, "Safe mode active (cycle %d) - VR: %.1f°C ASIC1: %.1f°C ASIC2: %.1f°C",
                             cooling_cycles, power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
                    
                    // Continue if ASIC temps still too high
                    if ((temp_is_plausible(power_management->chip_temp_avg) && power_management->chip_temp_avg > SAFE_TEMP)
                        || (temp_is_plausible(power_management->chip_temp2_avg) && power_management->chip_temp2_avg > SAFE_TEMP)) {
                        cooling_cycles = 0; // Reset cycle count if still hot
                    }
                } else {
                    // For boards using ASIC thermal diode (600 series), rely on VR temp and time
                    ESP_LOGW(TAG, "Safe mode active (cycle %d/%d) - VR: %.1f°C (ASIC temps unavailable while powered down)",
                             cooling_cycles, MIN_COOLING_CYCLES, power_management->vr_temp);
                }
            }
            ESP_LOGI(TAG, "Temperature normalized after %d cooling cycles. Reinitializing ASIC...", cooling_cycles);
            
            uint16_t reduced_voltage = last_known_asic_voltage > ASIC_REDUCTION ? last_known_asic_voltage - ASIC_REDUCTION : 1000;
            float reduced_asic_frequency = last_known_asic_frequency > ASIC_REDUCTION ? last_known_asic_frequency - ASIC_REDUCTION : 400.0;

            // Never drop below the regulator's minimum core voltage. TPS546_set_vout()
            // rejects anything lower (out of range), which leaves the VR stuck in a
            // "power fault" — and the invalid value is persisted to NVS, so it survives
            // reboots. Frequency reduction still provides the cooling headroom.
            int16_t min_voltage = VCORE_get_voltage_min_mv(GLOBAL_STATE);
            if (min_voltage > 0 && reduced_voltage < min_voltage) {
                reduced_voltage = (uint16_t) min_voltage;
            }

            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, reduced_voltage);
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, reduced_asic_frequency);
            
            ESP_LOGI(TAG, "Restoring at reduced settings: %umV (was %umV), %.0f MHz (was %.0f MHz)",
                     reduced_voltage, last_known_asic_voltage, reduced_asic_frequency, last_known_asic_frequency);

            uint8_t chip_count = mining_start(GLOBAL_STATE);

            // The chain was restarted: give it the warm-up grace again.
            watchdog.started_us = esp_timer_get_time();
            watchdog_reset_signals(&watchdog, sys_module, watchdog.started_us);

            if (chip_count > 0) {
                // Frequency reduction will now be applied by normal power management loop
                nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, false);
                ESP_LOGI(TAG, "Resuming normal operation. Reduced frequency (%.0f MHz) will be applied automatically.", reduced_asic_frequency);
            }
        }

        uint16_t core_voltage = GLOBAL_STATE->SELF_TEST_MODULE.is_active
                                 ? GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv
                                 : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
        float asic_frequency = GLOBAL_STATE->SELF_TEST_MODULE.is_active
                                 ? GLOBAL_STATE-> DEVICE_CONFIG.family.asic.default_frequency_mhz
                                 : nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage(GLOBAL_STATE, (double) core_voltage / 1000.0);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", asic_frequency, last_asic_frequency);
            
            power_management->frequency_value = asic_frequency;
            power_management->expected_hashrate = expected_hashrate(GLOBAL_STATE);

            ASIC_set_frequency(GLOBAL_STATE);
            ASIC_set_nonce_space(GLOBAL_STATE);
            uint8_t difficulty_power = calculate_effective_asic_difficulty(power_management->expected_hashrate);
            ASIC_set_difficulty(GLOBAL_STATE, difficulty_power);
            
            last_asic_frequency = asic_frequency;
        }

        // A chain that draws power but produces nothing must not be left
        // running: restart it, or fault out if restarting does not help.
        watchdog_tick(&watchdog, GLOBAL_STATE, is_paused);

        // Check for changing of overheat mode
        bool new_overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
        
        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
