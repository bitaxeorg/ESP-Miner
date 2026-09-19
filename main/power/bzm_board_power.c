#include "bzm_board_power.h"
#include "global_state.h"
#include "bonanza_power_task.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "bonanza_tps546.h"
#include "bzm_driver.h"
#include "bzm_frequency.h"
#include "bzm_lease_guard.h"
#include "bzm_power.h"
#include "bzm_runtime_health.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_config.h"
#include "thermal.h"
#include "bonanza_vcore.h"

#define BZM_MONITOR_PERIOD_MS 100U
#define BZM_HEALTH_PERIOD_MS 500U
#define BZM_SAFE_OFF_TIMEOUT_MS 1500U
#define BZM_SAFE_OFF_SAMPLE_MS 25U
#define BZM_BOARD_WATCHDOG_MS 300000U
#define BZM_IO_TASK_PRIORITY 18U
#define BZM_WORK_REPLACEMENT_TIMEOUT_MS 30000U

/* Board 1002 operating limits. CH2 is an inter-stack differential. */
#define BZM_FAN_MIN_RPM 1000U
#define BZM_SAFE_OFF_VCORE_MV 250U
#define BZM_TEMP_MIN_C (-20)
#define BZM_TEMP_MAX_C 75
#define BZM_STACK_MV_MIN 300U
#define BZM_STACK_MV_MAX 800U
#define BZM_INTERSTACK_DIFF_ABS_MAX_MV 50U
#define BZM_STACK_MAX_SPREAD_MV 100U
/* Confirm noisy CH2/combined-PLL samples; trips and other faults act at once. */
#define BZM_CH2_CONFIRM_SAMPLES 3U
#define BZM_PLL_LOCK_CONFIRM_SAMPLES 3U

typedef struct
{
    pthread_mutex_t lock;
    GlobalState * global_state;
    bool initialized;
    bool running;
    bool safe_off;
    bool fault_latched;
    char fault_detail[160];
    uint64_t watchdog_deadline_ms;
    bzm_bridge_safety_status_t bridge_status;
    bool bridge_status_valid;
    uint32_t replacement_generation;
    uint64_t replacement_started_ms;
    bool replacement_pending;
    atomic_bool pause_requested;
    atomic_bool dispatch_enabled;
    atomic_uint_fast64_t dispatch_deadline_ms;
    atomic_uint_fast64_t execution_deadline_ms;
    atomic_bool execution_cancelled;
    uint64_t health_sampled_at_ms;
    bzm_runtime_health_result_t health;
    float board_temperature_c;
    bzm_ch2_confirmation_t ch2_confirmation;
    bzm_pll_lock_confirmation_t pll_lock_confirmation;
    float frequency_target_mhz;
    float voltage_target_v;
    uint32_t frequency_target_generation;
    bool frequency_ramp_active;
    float rail_command_v;
    uint16_t fan_rpm;
} bzm_runtime_state_t;

static const char * TAG = "bzm_board_power";
static bool bridge_control_contract_compatible(
    const bzm_bridge_safety_status_t *status);
static bzm_runtime_state_t *RUNTIME_STATE;
#define RUNTIME (*RUNTIME_STATE)

static bool runtime_state_init(GlobalState *global_state)
{
    if (global_state == NULL ||
        global_state->DEVICE_CONFIG.family.asic.id != BZM ||
        !(global_state->DEVICE_CONFIG.family.id == BONANZA)) {
        return false;
    }
    if (RUNTIME_STATE != NULL) return true;

    bzm_runtime_state_t *allocated = heap_caps_calloc(
        1, sizeof(*allocated), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (allocated == NULL) {
        ESP_LOGE(TAG, "Unable to allocate Bonanza board state in PSRAM");
        return false;
    }
    if (pthread_mutex_init(&allocated->lock, NULL) != 0) {
        heap_caps_free(allocated);
        return false;
    }
    RUNTIME_STATE = allocated;
    ESP_LOGI(TAG, "Allocated %u bytes of Bonanza board state in PSRAM",
             (unsigned)sizeof(*allocated));
    return true;
}

static bzm_bringup_telemetry_policy_t telemetry_policy(void);

static bool start_hardware_locked(void);
static bool shutdown_locked(void);
static bool latch_fault_locked(const char *detail);
static bool board_error_locked(const char *detail);
static bzm_runtime_health_result_t sample_runtime_health_locked(void);
static bool recoverable_overheat_fault(
    bzm_runtime_health_fault_t fault);

static uint64_t now_ms(void)
{
    return (uint64_t) (esp_timer_get_time() / 1000);
}

static void close_dispatch_locked(void)
{
    atomic_store_explicit(&RUNTIME.dispatch_enabled, false, memory_order_release);
    atomic_store_explicit(&RUNTIME.dispatch_deadline_ms, 0, memory_order_release);
}

static bool runtime_dispatch_authorizer(void * context)
{
    bzm_runtime_state_t * runtime = context;
    if (runtime == NULL || BZM_has_io_fault() || BONANZA_POWER_MANAGEMENT_stop_requested() ||
        (runtime->global_state != NULL && runtime->global_state->SYSTEM_MODULE.pools_unavailable) ||
        atomic_load_explicit(&runtime->pause_requested,
                             memory_order_acquire) ||
        !atomic_load_explicit(&runtime->dispatch_enabled,
                              memory_order_acquire)) {
        return false;
    }
    uint64_t deadline = atomic_load_explicit(&runtime->dispatch_deadline_ms, memory_order_acquire);
    return deadline != 0 && now_ms() < deadline;
}

static bool runtime_execution_authorizer(void * context)
{
    bzm_runtime_state_t * runtime = context;
    if (runtime == NULL) return false;
    if (BONANZA_POWER_MANAGEMENT_stop_requested() ||
        (runtime->global_state != NULL && runtime->global_state->SYSTEM_MODULE.pools_unavailable)) {
        atomic_store_explicit(&runtime->execution_cancelled, true, memory_order_release);
        return false;
    }
    uint64_t deadline = atomic_load_explicit(&runtime->execution_deadline_ms, memory_order_acquire);
    return bzm_lease_guard_deadline_allows(deadline, now_ms());
}

static bool dispatch_allowed_locked(uint64_t current_ms)
{
    return RUNTIME.running && !RUNTIME.fault_latched &&
           RUNTIME.health.status != BZM_RUNTIME_HEALTH_BAD &&
           bzm_lease_guard_deadline_allows(RUNTIME.watchdog_deadline_ms, current_ms);
}

static void sync_dispatch_locked(void)
{
    if (!dispatch_allowed_locked(now_ms())) {
        close_dispatch_locked();
        return;
    }
    atomic_store_explicit(&RUNTIME.dispatch_deadline_ms, RUNTIME.watchdog_deadline_ms, memory_order_release);
    atomic_store_explicit(&RUNTIME.dispatch_enabled, true, memory_order_release);
}

static bool start_production_mining_locked(void)
{
    close_dispatch_locked();
    bzm_ch2_confirmation_init(&RUNTIME.ch2_confirmation);
    bzm_pll_lock_confirmation_init(&RUNTIME.pll_lock_confirmation);
    uint64_t deadline = 0;
    if (!bzm_lease_guard_make_deadline(now_ms(), BZM_BOARD_WATCHDOG_MS, &deadline)) return false;
    atomic_store_explicit(&RUNTIME.execution_deadline_ms, deadline, memory_order_release);
    bool completed = start_hardware_locked();
    atomic_store_explicit(&RUNTIME.execution_deadline_ms, 0, memory_order_release);
    if (completed) {
        RUNTIME.running = true;
        RUNTIME.watchdog_deadline_ms = deadline;
        bzm_runtime_health_result_t health = sample_runtime_health_locked();
        completed = health.status == BZM_RUNTIME_HEALTH_GOOD;
        if (!completed) (void)board_error_locked(health.detail);
    }
    if (!completed) {
        if (!atomic_load_explicit(&RUNTIME.execution_cancelled, memory_order_acquire)) {
            RUNTIME.fault_latched = true;
        }
        (void)shutdown_locked();
    } else {
        RUNTIME.global_state->ASIC_initalized = true;
        /* Wait for the initial work rotation before applying a live clock change.
         * This synchronizes hardware writes without requiring a lucky nonce. */
        uint32_t completed_generation = 0;
        (void)BZM_work_replacement_snapshot(&RUNTIME.replacement_generation,
                                            &completed_generation, &RUNTIME.replacement_pending);
        RUNTIME.replacement_started_ms = now_ms();
    }
    sync_dispatch_locked();
    return completed;
}

static bzm_runtime_health_result_t sample_runtime_health_locked(void)
{
    bzm_runtime_health_input_t input = {
        .running = RUNTIME.running,
        .bridge_status_available = RUNTIME.bridge_status_valid,
        .bridge_status = RUNTIME.bridge_status,
        .fan_min_rpm = BZM_FAN_MIN_RPM,
        .tps_bounds =
            {
                .vin_min_v = BZM_TPS546_BIRDS_PROFILE.vin_off,
                .vin_max_v = BZM_TPS546_BIRDS_PROFILE.vin_ov_fault_limit,
                .vout_command_v = RUNTIME.rail_command_v,
                .vout_command_tolerance_v = BZM_TPS546_VOUT_READBACK_TOLERANCE_V,
                .vout_min_v =
                    RUNTIME.rail_command_v -
                    BZM_TPS546_VOUT_OPERATING_TOLERANCE_V,
                .vout_max_v =
                    RUNTIME.rail_command_v +
                    BZM_TPS546_VOUT_OPERATING_TOLERANCE_V,
                .iout_min_a = -1.0f,
                .iout_max_a = BZM_TPS546_BIRDS_PROFILE.iout_oc_warn_limit,
                .temperature_min_c = -40.0f,
                .temperature_max_c =
                    (float)BZM_TPS546_BIRDS_PROFILE.ot_warn_limit,
            },
        .telemetry_bounds = telemetry_policy().bounds,
        .telemetry_now_us = (uint64_t) esp_timer_get_time(),
        .telemetry_max_age_us = telemetry_policy().max_age_us,
        .defer_ch2_bounds = true,
        .defer_clock_locks = true,
    };

    if (input.running) {
        bzm_bridge_safety_status_t status;
        input.bridge_status_available = BZM_bridge_safety_heartbeat(&status) == ESP_OK && status.valid;
        if (input.bridge_status_available) {
            input.bridge_status = status;
            RUNTIME.bridge_status = status;
            RUNTIME.bridge_status_valid = true;
            if (RUNTIME.global_state != NULL) {
                RUNTIME.global_state->POWER_MANAGEMENT_MODULE.fan_perc =
                    status.fan_percent;
            }
        } else {
            RUNTIME.bridge_status_valid = false;
        }
        input.fan_tach_available = BZM_bridge_get_fan_rpm(&input.fan_rpm) == ESP_OK;
        if (input.fan_tach_available) {
            RUNTIME.fan_rpm = input.fan_rpm;
            if (RUNTIME.global_state != NULL) {
                RUNTIME.global_state->POWER_MANAGEMENT_MODULE.fan_rpm =
                    input.fan_rpm;
            }
        }
    }

    if (input.running) {
        BONANZA_TPS546_StatusSnapshot power = {0};
        bool pgood = false;
        input.tps.available = BONANZA_VCORE_bzm_snapshot(&power, &pgood) == ESP_OK;
        if (input.tps.available) {
            input.tps.pgood = pgood;
            input.tps.operation = power.operation;
            input.tps.status_word = power.status_word;
            input.tps.vout_command_v = power.vout_command;
            input.tps.vout_command_matches_expected =
                isfinite(power.vout_command) &&
                fabsf(power.vout_command - RUNTIME.rail_command_v) <=
                    BZM_TPS546_VOUT_READBACK_TOLERANCE_V;
            input.tps.vin_v = power.read_vin;
            input.tps.vout_v = power.read_vout;
            input.tps.iout_a = power.read_iout;
            input.tps.temperature_c = power.read_temp1;
            if (RUNTIME.global_state != NULL) {
                PowerManagementModule *management =
                    &RUNTIME.global_state->POWER_MANAGEMENT_MODULE;
                management->voltage = power.read_vin * 1000.0f;
                management->core_voltage = power.read_vout * 1000.0f;
                management->current = power.read_iout * 1000.0f;
                management->power =
                    power.read_vout * power.read_iout +
                    RUNTIME.global_state->DEVICE_CONFIG.family.power_offset;
                management->vr_temp = power.read_temp1;
            }
        }
    }

    if (input.running) {
        input.telemetry_available = BZM_get_telemetry_snapshot(&input.telemetry);
        /* Timestamp the completed snapshot, not the start of the copy. The
         * transport can publish a fresh sample while this task is waiting for
         * its mutex; taking `now` first made that valid sample appear a few
         * hundred microseconds in the future and falsely latched safe-off. */
        input.telemetry_now_us = (uint64_t) esp_timer_get_time();
        if (input.telemetry_available) {
            const bool temperature_valid = bzm_telemetry_max_temperature(
                &input.telemetry, input.telemetry_now_us,
                input.telemetry_max_age_us, &RUNTIME.board_temperature_c);
            if (temperature_valid && RUNTIME.global_state != NULL) {
                RUNTIME.global_state->POWER_MANAGEMENT_MODULE.chip_temp_avg =
                    RUNTIME.board_temperature_c;
                RUNTIME.global_state->POWER_MANAGEMENT_MODULE.chip_temp2_avg =
                    -1.0f;
            }
        }
    }

    RUNTIME.health = bzm_runtime_health_evaluate(&input);
    if (input.running && RUNTIME.health.status == BZM_RUNTIME_HEALTH_GOOD && BZM_has_io_fault()) {
        RUNTIME.health.status = BZM_RUNTIME_HEALTH_BAD;
        RUNTIME.health.fault = BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT;
        snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail), "ASIC work transport failed");
    }
    if (RUNTIME.health.status == BZM_RUNTIME_HEALTH_GOOD && input.running &&
        input.telemetry_available) {
        uint8_t culprit_asic_id = 0;
        uint8_t observed_samples = 0;
        bzm_ch2_confirmation_result_t confirmation = bzm_pll_lock_confirmation_observe(
            &RUNTIME.pll_lock_confirmation, &input.telemetry, input.telemetry_now_us, input.telemetry_max_age_us,
            BZM_PLL_LOCK_CONFIRM_SAMPLES, &culprit_asic_id, &observed_samples);
        if (confirmation == BZM_CH2_CONFIRMATION_CONTINUOUS || confirmation == BZM_CH2_CONFIRMATION_INVALID) {
            RUNTIME.health.status = BZM_RUNTIME_HEALTH_BAD;
            RUNTIME.health.fault = BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_CLOCK_UNLOCKED;
            if (confirmation == BZM_CH2_CONFIRMATION_CONTINUOUS) {
                snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail),
                         "ASIC 0x%02x combined PLL lock clear continuously for %u/%u fresh samples",
                         (unsigned) culprit_asic_id, (unsigned) observed_samples,
                         (unsigned) BZM_PLL_LOCK_CONFIRM_SAMPLES);
            } else {
                snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail),
                         "combined PLL lock confirmation input is invalid");
            }
        } else if (observed_samples != 0) {
            RUNTIME.health.fault = BZM_RUNTIME_HEALTH_FAULT_NONE;
            snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail),
                     confirmation == BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE
                         ? "ASIC 0x%02x combined PLL lock clear pending a fresh sample at %u/%u"
                         : "ASIC 0x%02x combined PLL lock clear pending confirmation at %u/%u",
                     (unsigned) culprit_asic_id, (unsigned) observed_samples,
                     (unsigned) BZM_PLL_LOCK_CONFIRM_SAMPLES);
        }
    }
    if (RUNTIME.health.status == BZM_RUNTIME_HEALTH_GOOD && input.running &&
        input.telemetry_available) {
        uint8_t culprit_asic_id = 0;
        uint8_t observed_samples = 0;
        bzm_ch2_confirmation_result_t confirmation =
            bzm_ch2_confirmation_observe(&RUNTIME.ch2_confirmation, &input.telemetry, &input.telemetry_bounds,
                                         BZM_CH2_CONFIRM_SAMPLES, &culprit_asic_id, &observed_samples);
        if (confirmation == BZM_CH2_CONFIRMATION_CONTINUOUS || confirmation == BZM_CH2_CONFIRMATION_INVALID) {
            RUNTIME.health.status = BZM_RUNTIME_HEALTH_BAD;
            RUNTIME.health.fault = BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS;
            if (confirmation == BZM_CH2_CONFIRMATION_CONTINUOUS) {
                const bzm_telemetry_sample_t * sample = bzm_telemetry_store_get(&input.telemetry, culprit_asic_id);
                snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail),
                         "ASIC 0x%02x CH2 excursion continuous for %u/%u fresh samples: %.1f mV limit=+/-%.1f mV",
                         (unsigned) culprit_asic_id, (unsigned) observed_samples, (unsigned) BZM_CH2_CONFIRM_SAMPLES,
                         sample != NULL ? sample->ch2_mv : NAN, input.telemetry_bounds.ch2_abs_max_mv);
            } else {
                snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail), "CH2 confirmation input is invalid");
            }
        } else if (observed_samples != 0) {
            RUNTIME.health.fault = BZM_RUNTIME_HEALTH_FAULT_NONE;
            snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail),
                     confirmation == BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE
                         ? "ASIC 0x%02x CH2 excursion pending a fresh sample at %u/%u"
                         : "ASIC 0x%02x CH2 excursion pending confirmation at %u/%u",
                     (unsigned) culprit_asic_id, (unsigned) observed_samples, (unsigned) BZM_CH2_CONFIRM_SAMPLES);
        }
    }
    RUNTIME.health_sampled_at_ms = now_ms();
    return RUNTIME.health;
}

static bool bridge_status_runtime_good(const bzm_bridge_safety_status_t * status)
{
    return status != NULL && status->valid && status->fault == BZM_BRIDGE_SAFETY_FAULT_NONE && !status->trip_input_asserted &&
           (status->runtime_verdict == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF ||
            status->runtime_verdict == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED);
}

static bool bridge_control_contract_compatible(
    const bzm_bridge_safety_status_t *status)
{
    const uint16_t required = BZM_BRIDGE_SAFETY_CAP_5V_CONTROL |
                              BZM_BRIDGE_SAFETY_CAP_ASIC_RESET_CONTROL |
                              BZM_BRIDGE_SAFETY_CAP_FAN_FORCE_FULL |
                              BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED |
                              BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED;
    return bridge_status_runtime_good(status) &&
           status->stage == BZM_BRIDGE_SAFETY_STAGE_TRIP_LATCH &&
           (status->capabilities & required) == required;
}

static bool board_error_locked(const char *detail)
{
    if (!RUNTIME.fault_latched || RUNTIME.fault_detail[0] == '\0') {
        snprintf(RUNTIME.fault_detail, sizeof(RUNTIME.fault_detail), "%s", detail);
    }
    ESP_LOGE(TAG, "%s", detail);
    return false;
}

static bool shutdown_locked(void)
{
    GlobalState *state = RUNTIME.global_state;
    bool commands_ok = state != NULL;
    bzm_bridge_safety_status_t status;

    close_dispatch_locked();
    RUNTIME.running = false;
    RUNTIME.safe_off = false;
    RUNTIME.watchdog_deadline_ms = 0;

    if (state == NULL) {
        RUNTIME.fault_latched = true;
        return board_error_locked("global state is unavailable; shutdown cannot be verified");
    }

    state->ASIC_initalized = false;
    (void) BZM_hold_reset();
    BZM_set_dispatch_authorizer(runtime_dispatch_authorizer, &RUNTIME);

    /* Disarm is best-effort for recovery from legacy unversioned firmware. */
    (void) BZM_bridge_disarm_safety(&status);
    commands_ok = BZM_bridge_set_asic_reset(false) == ESP_OK && commands_ok;
    commands_ok = BZM_bridge_set_5v_enabled(false) == ESP_OK && commands_ok;
    commands_ok = Thermal_set_fan_percent(&state->DEVICE_CONFIG, 1.0f) == ESP_OK && commands_ok;
    commands_ok = BONANZA_VCORE_bzm_set_rail_enabled(state, false) == ESP_OK && commands_ok;

    BONANZA_TPS546_StatusSnapshot power = {0};
    bool pgood = true;
    bool electrical_safe = false;
    for (uint32_t waited = 0; waited <= BZM_SAFE_OFF_TIMEOUT_MS; waited += BZM_SAFE_OFF_SAMPLE_MS) {
        if (BONANZA_VCORE_bzm_snapshot(&power, &pgood) == ESP_OK && !pgood && (power.operation & OPERATION_ON) == 0 &&
            power.read_vout * 1000.0f <= (float) BZM_SAFE_OFF_VCORE_MV) {
            electrical_safe = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(BZM_SAFE_OFF_SAMPLE_MS));
    }

    RUNTIME.bridge_status_valid = BZM_bridge_get_safety_status(&RUNTIME.bridge_status) == ESP_OK && RUNTIME.bridge_status.valid;
    if (electrical_safe && RUNTIME.bridge_status_valid &&
        RUNTIME.bridge_status.fault != BZM_BRIDGE_SAFETY_FAULT_NONE &&
        bzm_bridge_safety_status_allows_fault_clear(&RUNTIME.bridge_status)) {
        /* The RP2040 intentionally retains lease/trip faults across an ESP
         * reboot. Clear that latch only after this boot has independently
         * forced safe bridge outputs and proved the TPS rail off and
         * discharged, then evaluate the returned status from scratch. */
        bzm_bridge_safety_status_t cleared = {0};
        esp_err_t clear_err = BZM_bridge_clear_safety_fault(&cleared);
        commands_ok = clear_err == ESP_OK && commands_ok;
        RUNTIME.bridge_status = cleared;
        RUNTIME.bridge_status_valid = clear_err == ESP_OK && cleared.valid;
        if (RUNTIME.bridge_status_valid) {
            ESP_LOGW(TAG, "Cleared retained bridge safety fault after verified electrical safe-off");
        } else {
            ESP_LOGE(TAG, "Retained bridge safety fault clear failed: %s", esp_err_to_name(clear_err));
        }
    }
    const uint16_t required_safe_evidence =
        BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE | BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR;
    bool bridge_safe = RUNTIME.bridge_status_valid && RUNTIME.bridge_status.state == BZM_BRIDGE_SAFETY_STATE_SAFE_OFF &&
                       RUNTIME.bridge_status.fault == BZM_BRIDGE_SAFETY_FAULT_NONE &&
                       RUNTIME.bridge_status.runtime_verdict == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF &&
                       RUNTIME.bridge_status.lease_remaining_ms == 0 && !RUNTIME.bridge_status.five_volt_enabled &&
                       RUNTIME.bridge_status.asic_reset_asserted && RUNTIME.bridge_status.fan_full &&
                       RUNTIME.bridge_status.fan_percent == 100 && !RUNTIME.bridge_status.trip_input_asserted &&
                       (RUNTIME.bridge_status.evidence & required_safe_evidence) == required_safe_evidence;

    if (!commands_ok || !electrical_safe || !bridge_safe) {
        char detail[sizeof(RUNTIME.fault_detail)];
        snprintf(detail, sizeof(detail),
                 "safe-off failed: commands=%s pgood=%s vout=%.3fV "
                 "operation=0x%02x bridge=%s",
                 commands_ok ? "ok" : "bad", pgood ? "high" : "low", power.read_vout, power.operation,
                 bridge_safe ? "safe" : (RUNTIME.bridge_status_valid ? "unsafe" : "unavailable"));
        RUNTIME.fault_latched = true;
        return board_error_locked(detail);
    }

    RUNTIME.safe_off = true;
    return true;
}

static bool latch_fault_locked(const char *detail)
{
    (void)board_error_locked(detail);
    RUNTIME.fault_latched = true;
    return shutdown_locked();
}

static bool prepare_bridge_locked(GlobalState *state)
{
    bzm_bridge_info_t info;
    bzm_bridge_safety_status_t status;
    if (BZM_bridge_get_info(&info) != ESP_OK || !bzm_bridge_info_supports_safety(&info) ||
        BZM_bridge_get_safety_status(&status) != ESP_OK ||
        status.state != BZM_BRIDGE_SAFETY_STATE_SAFE_OFF ||
        !bridge_control_contract_compatible(&status)) {
        return board_error_locked("bridge protocol or safe output state is incompatible");
    }
    RUNTIME.safe_off = false;
    if (BZM_bridge_arm_safety(&status) != ESP_OK ||
        !bzm_lease_guard_status_is_controlled(&status) ||
        status.five_volt_enabled || !status.asic_reset_asserted || !status.fan_full ||
        Thermal_set_fan_percent(&state->DEVICE_CONFIG, 1.0f) != ESP_OK ||
        BZM_bridge_get_fan_rpm(&RUNTIME.fan_rpm) != ESP_OK || RUNTIME.fan_rpm < BZM_FAN_MIN_RPM) {
        return board_error_locked("bridge arm or startup fan check failed");
    }
    return true;
}

static bool enable_power_rail_locked(GlobalState * state)
{
    bzm_bridge_safety_status_t status;
    if (BONANZA_VCORE_bzm_set_rail_enabled(state, true) != ESP_OK) {
        return board_error_locked("TPS rail enable or live power validation failed");
    }

    char profile_detail[128];
    if (BONANZA_TPS546_check_protection(profile_detail, sizeof(profile_detail)) != ESP_OK) {
        char detail[sizeof(RUNTIME.fault_detail)];
        snprintf(detail, sizeof(detail), "TPS profile readback mismatch: %s", profile_detail);
        return board_error_locked(detail);
    }

    BONANZA_TPS546_StatusSnapshot power = {0};
    bool pgood = false;
    if (BONANZA_VCORE_bzm_snapshot(&power, &pgood) != ESP_OK || !pgood || (power.operation & OPERATION_ON) == 0 ||
        !power.vout_command_matches_active_config || !isfinite(power.vout_command) ||
        fabsf(power.vout_command - BZM_TPS546_FIXED_VOUT_V) > BZM_TPS546_VOUT_READBACK_TOLERANCE_V ||
        !isfinite(power.read_vin) || !isfinite(power.read_vout) || !isfinite(power.read_iout) ||
        power.read_vin < BZM_TPS546_BIRDS_PROFILE.vin_off || power.read_vout < 2.65f || power.read_vout > 2.95f ||
        power.read_iout < -1.0f || power.read_iout > BZM_TPS546_BIRDS_PROFILE.iout_oc_warn_limit ||
        power.read_temp1 > BZM_TPS546_BIRDS_PROFILE.ot_warn_limit || power.status_word != 0) {
        char detail[sizeof(RUNTIME.fault_detail)];
        snprintf(detail, sizeof(detail),
                 "TPS startup failed: PGOOD=%u OP=0x%02x STATUS=0x%04x CMD=%.3fV RAW=0x%04x EXACT=%u VIN=%.2fV VOUT=%.3fV IOUT=%.2fA TEMP=%dC",
                 (unsigned) pgood, (unsigned) power.operation, (unsigned) power.status_word,
                 power.vout_command, (unsigned) power.vout_command_raw,
                 (unsigned) power.vout_command_matches_active_config, power.read_vin,
                 power.read_vout, power.read_iout, power.read_temp1);
        return board_error_locked(detail);
    }
    if (BZM_bridge_get_safety_status(&status) != ESP_OK || !status.valid || status.state != BZM_BRIDGE_SAFETY_STATE_CONTROLLED ||
        status.five_volt_enabled || !status.asic_reset_asserted || !status.fan_full || !bridge_status_runtime_good(&status)) {
        return board_error_locked("bridge outputs changed unexpectedly while the TPS rail was validated");
    }
    RUNTIME.bridge_status = status;
    RUNTIME.bridge_status_valid = true;
    return true;
}

static bzm_bringup_telemetry_policy_t telemetry_policy(void)
{
    return (bzm_bringup_telemetry_policy_t){
        .bounds =
            {
                .temperature_min_c = (float) BZM_TEMP_MIN_C,
                .temperature_max_c = (float) BZM_TEMP_MAX_C,
                .ch0_min_mv = (float) BZM_STACK_MV_MIN,
                .ch0_max_mv = (float) BZM_STACK_MV_MAX,
                .ch1_min_mv = (float) BZM_STACK_MV_MIN,
                .ch1_max_mv = (float) BZM_STACK_MV_MAX,
                .ch2_abs_max_mv = (float) BZM_INTERSTACK_DIFF_ABS_MAX_MV,
                .max_stack_spread_mv = (float) BZM_STACK_MAX_SPREAD_MV,
            },
        .max_age_us = BZM_TELEMETRY_MAX_AGE_US,
        .ch2_confirm_samples = BZM_CH2_CONFIRM_SAMPLES,
    };
}

static bool bringup_succeeded(const char *operation, bzm_bringup_outcome_t outcome,
                              const bzm_bringup_report_t *report)
{
    if (outcome == BZM_BRINGUP_GOOD) return true;
    char detail[sizeof(RUNTIME.fault_detail)];
    snprintf(detail, sizeof(detail), "%s failed: %s ASIC=0x%02x PLL=%u reg=0x%02x expected=%lu actual=%lu",
             operation, bzm_bringup_reason_name(report->reason), report->asic_id,
             report->pll_index, report->register_offset,
             (unsigned long)report->expected, (unsigned long)report->actual);
    return board_error_locked(detail);
}

static bool start_hardware_locked(void)
{
    GlobalState *state = RUNTIME.global_state;
    bzm_bridge_safety_status_t status;
    bzm_bringup_report_t report = {0};
    bzm_bringup_telemetry_policy_t policy = telemetry_policy();

    if (!shutdown_locked() || !runtime_execution_authorizer(&RUNTIME) ||
        !prepare_bridge_locked(state) || !runtime_execution_authorizer(&RUNTIME) ||
        !enable_power_rail_locked(state) || !runtime_execution_authorizer(&RUNTIME)) return false;
    if (BZM_bridge_set_asic_reset(false) != ESP_OK || BZM_bridge_set_5v_enabled(true) != ESP_OK ||
        BZM_bridge_get_safety_status(&status) != ESP_OK ||
        !bzm_lease_guard_status_is_controlled(&status) || !status.five_volt_enabled ||
        !status.asic_reset_asserted || !status.fan_full) {
        return board_error_locked("chain power/reset startup failed");
    }
    if (!bringup_succeeded("ASIC startup", BZM_start(state, &policy, &report), &report)) return false;
    state->POWER_MANAGEMENT_MODULE.actual_frequency = BZM_FREQUENCY_POWER_ON_MHZ;
    BZM_set_dispatch_authorizer(runtime_dispatch_authorizer, &RUNTIME);
    return runtime_execution_authorizer(&RUNTIME);
}

static float expected_bzm_hashrate_ghs(const GlobalState *state,
                                       float frequency_mhz)
{
    if (state == NULL || !isfinite(frequency_mhz)) return 0.0f;
    return frequency_mhz * state->DEVICE_CONFIG.family.asic.core_count *
           state->DEVICE_CONFIG.family.asic_count * 4.0f / 3.0f / 1000.0f;
}

static bool configured_tuning_target(
    bzm_frequency_target_t *frequency_target, float *voltage_target_v)
{
    if (frequency_target == NULL || voltage_target_v == NULL) return false;

    const float requested_mhz =
        (RUNTIME.global_state->SELF_TEST_MODULE.is_active ? 800.0f : nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY));
    const uint16_t requested_voltage_mv =
        (RUNTIME.global_state->SELF_TEST_MODULE.is_active ? 2800 : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE));
    return bzm_frequency_request_is_valid(requested_mhz) &&
           bzm_frequency_resolve_target(requested_mhz, frequency_target) &&
           bzm_power_resolve_user_voltage(requested_voltage_mv,
                                          voltage_target_v);
}

static bool tuning_snapshot(bonanza_power_target_t target, float *target_mhz,
                                    float *target_voltage_v,
                                    uint32_t *generation)
{
    pthread_mutex_lock(&RUNTIME.lock);
    bzm_frequency_target_t frequency_target;
    float requested_voltage_v = 0.0f;
    const bool target_valid = bzm_frequency_request_is_valid(target.frequency_mhz) &&
        bzm_frequency_resolve_target(target.frequency_mhz, &frequency_target) &&
        bzm_power_resolve_user_voltage(target.voltage_mv, &requested_voltage_v);
    const bool target_changed =
        target_valid &&
        (fabsf(RUNTIME.frequency_target_mhz -
               frequency_target.actual_mhz) >= 0.001f ||
         fabsf(RUNTIME.voltage_target_v - requested_voltage_v) >= 0.001f);
    if (target_changed) {
        RUNTIME.frequency_target_mhz = frequency_target.actual_mhz;
        RUNTIME.voltage_target_v = requested_voltage_v;
        ++RUNTIME.frequency_target_generation;
        RUNTIME.frequency_ramp_active = true;
        if (RUNTIME.global_state != NULL) {
            RUNTIME.global_state->POWER_MANAGEMENT_MODULE.frequency_value =
                frequency_target.actual_mhz;
            RUNTIME.global_state->POWER_MANAGEMENT_MODULE.expected_hashrate =
                expected_bzm_hashrate_ghs(
                    RUNTIME.global_state, frequency_target.actual_mhz);
        }
        ESP_LOGI(TAG,
                 "BZM live mining target changed to %.3f MHz, %.3f V "
                 "(generation %lu)",
                 frequency_target.actual_mhz, requested_voltage_v,
                 (unsigned long)RUNTIME.frequency_target_generation);
    }

    const uint64_t current_ms = now_ms();
    const bool ready =
        target_valid && RUNTIME.initialized && RUNTIME.global_state != NULL &&
        !atomic_load_explicit(&RUNTIME.pause_requested,
                              memory_order_acquire) &&
        RUNTIME.running &&
        !RUNTIME.fault_latched &&
        dispatch_allowed_locked(current_ms);
    if (target_mhz != NULL) *target_mhz = RUNTIME.frequency_target_mhz;
    if (target_voltage_v != NULL) {
        *target_voltage_v = RUNTIME.voltage_target_v;
    }
    if (generation != NULL) {
        *generation = RUNTIME.frequency_target_generation;
    }
    if (!ready && !target_valid) RUNTIME.frequency_ramp_active = false;
    pthread_mutex_unlock(&RUNTIME.lock);
    return ready;
}

static void tuning_set_active(bool active, uint32_t generation)
{
    pthread_mutex_lock(&RUNTIME.lock);
    if (RUNTIME.frequency_target_generation == generation) {
        RUNTIME.frequency_ramp_active = active;
    }
    pthread_mutex_unlock(&RUNTIME.lock);
}

static float tuning_rail_command(void)
{
    pthread_mutex_lock(&RUNTIME.lock);
    const float command_v = RUNTIME.rail_command_v;
    pthread_mutex_unlock(&RUNTIME.lock);
    return command_v;
}

static bool frequency_apply_voltage(float target_v, uint32_t generation)
{
    pthread_mutex_lock(&RUNTIME.lock);
    const bool authorized =
        RUNTIME.initialized && RUNTIME.global_state != NULL &&
        !BONANZA_POWER_MANAGEMENT_stop_requested() &&
        !atomic_load_explicit(&RUNTIME.pause_requested,
                              memory_order_acquire) &&
        RUNTIME.running &&
        !RUNTIME.fault_latched &&
        RUNTIME.frequency_target_generation == generation &&
        dispatch_allowed_locked(now_ms());
    if (!authorized) {
        pthread_mutex_unlock(&RUNTIME.lock);
        return false;
    }

    const float previous_v = RUNTIME.rail_command_v;
    if (fabsf(previous_v - target_v) <=
        BZM_TPS546_VOUT_READBACK_TOLERANCE_V) {
        pthread_mutex_unlock(&RUNTIME.lock);
        return true;
    }

    /*
     * Publish the expected rail atomically with the bounded PMBus operation
     * so the safety monitor cannot compare a new command to stale state.
     */
    RUNTIME.rail_command_v = target_v;
    const bool applied =
        BONANZA_VCORE_bzm_set_runtime_voltage(RUNTIME.global_state, target_v) ==
        ESP_OK;
    if (atomic_load_explicit(&RUNTIME.pause_requested,
                             memory_order_acquire)) {
        pthread_mutex_unlock(&RUNTIME.lock);
        ESP_LOGI(TAG,
                 "BZM live voltage generation %lu was cancelled",
                 (unsigned long)generation);
        return false;
    }
    if (!applied) {
        char detail[sizeof(RUNTIME.fault_detail)];
        snprintf(detail, sizeof(detail),
                 "BZM runtime rail transition %.3fV -> %.3fV failed",
                 previous_v, target_v);
        close_dispatch_locked();
        (void)latch_fault_locked(detail);
        sync_dispatch_locked();
        pthread_mutex_unlock(&RUNTIME.lock);
        return false;
    }

    RUNTIME.health_sampled_at_ms = 0;
    pthread_mutex_unlock(&RUNTIME.lock);
    ESP_LOGI(TAG,
             "BZM live mining rail reached user target %.3f V "
             "for generation %lu",
             target_v, (unsigned long)generation);
    return true;
}

static bool frequency_domains_all_at(const bzm_bringup_state_t *state,
                                     float target_mhz)
{
    if (state == NULL) return false;
    for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
            if (fabsf(state->domain_clock_mhz[asic][pll] - target_mhz) >=
                0.001f) {
                return false;
            }
        }
    }
    return true;
}

static void frequency_domains_copy(
    float destination[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    const float source[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT])
{
    memcpy(destination, source,
           sizeof(float) * BZM_BRINGUP_ASIC_COUNT *
               BZM_BRINGUP_PLL_COUNT);
}

static bool frequency_transition_authorized_locked(uint32_t generation)
{
    return RUNTIME.initialized && RUNTIME.global_state != NULL &&
           !BONANZA_POWER_MANAGEMENT_stop_requested() &&
           !atomic_load_explicit(&RUNTIME.pause_requested,
                                 memory_order_acquire) &&
           RUNTIME.frequency_ramp_active &&
           RUNTIME.frequency_target_generation == generation &&
           RUNTIME.running &&
           !RUNTIME.fault_latched &&
           dispatch_allowed_locked(now_ms());
}

static bool frequency_apply_domains(
    const float
        frequency_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, uint32_t generation, float *actual_mhz)
{
    bzm_bringup_report_t report = {0};
    const bzm_bringup_outcome_t outcome =
        BZM_step_frequency_domains(
            frequency_mhz, allow_initial_jump, &report, actual_mhz);

    /* A pause can arrive while the bounded PLL operation owns the driver
     * reactor. It then waits for that operation and forces safe-off. Commit
     * no stale clock state, and latch no false transition fault,
     * unless this exact tuning generation still owns a healthy mining lease. */
    pthread_mutex_lock(&RUNTIME.lock);
    if (!frequency_transition_authorized_locked(generation)) {
        pthread_mutex_unlock(&RUNTIME.lock);
        ESP_LOGI(TAG,
                 "BZM live frequency generation %lu was cancelled",
                 (unsigned long)generation);
        return false;
    }

    if (outcome != BZM_BRINGUP_GOOD) {
        ESP_LOGE(TAG,
                 "Live domain-frequency transaction failed: reason=%s "
                 "asic=0x%02x pll=%u",
                 bzm_bringup_reason_name(report.reason), report.asic_id,
                 report.pll_index);
        char detail[sizeof(RUNTIME.fault_detail)];
        snprintf(
            detail, sizeof(detail),
            "live PLL transaction failed: %s ASIC=0x%02x PLL=%u",
            bzm_bringup_reason_name(report.reason), report.asic_id,
            report.pll_index);
        close_dispatch_locked();
        (void)latch_fault_locked(detail);
        sync_dispatch_locked();
        pthread_mutex_unlock(&RUNTIME.lock);
        return false;
    }

    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.actual_frequency =
        actual_mhz == NULL ? 0.0f : *actual_mhz;
    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.expected_hashrate =
        expected_bzm_hashrate_ghs(
            RUNTIME.global_state, RUNTIME.frequency_target_mhz);
    bzm_pll_lock_confirmation_init(&RUNTIME.pll_lock_confirmation);
    RUNTIME.health_sampled_at_ms = 0;
    pthread_mutex_unlock(&RUNTIME.lock);
    return true;
}

bool BZM_board_apply(void *context, bonanza_power_target_t target)
{
    (void)context;
    {
        float target_mhz = 0.0f;
        float target_voltage_v = 0.0f;
        uint32_t generation = 0;
        if (!tuning_snapshot(
                target, &target_mhz, &target_voltage_v, &generation)) {
            return true;
        }

        if (RUNTIME.replacement_pending) {
            uint32_t current_generation = 0;
            uint32_t completed = 0;
            bool pending = false;
            if (!BZM_work_replacement_snapshot(&current_generation, &completed, &pending)) return false;
            if (pending && (int32_t)(completed - RUNTIME.replacement_generation) < 0) {
                return now_ms() - RUNTIME.replacement_started_ms < BZM_WORK_REPLACEMENT_TIMEOUT_MS;
            }
            RUNTIME.replacement_pending = false;
        }
        bzm_bringup_state_t state;
        if (!BZM_get_state(&state) || !state.running) {
            return true;
        }

        const float rail_command_v = tuning_rail_command();
        if (target_voltage_v >
            rail_command_v + BZM_TPS546_VOUT_READBACK_TOLERANCE_V) {
            tuning_set_active(true, generation);
            if (!frequency_apply_voltage(target_voltage_v, generation)) {
                tuning_set_active(false, generation);
                return BONANZA_POWER_MANAGEMENT_stop_requested();
            }
            return true;
        }

        if (frequency_domains_all_at(&state, target_mhz)) {
            if (target_voltage_v <
                rail_command_v -
                    BZM_TPS546_VOUT_READBACK_TOLERANCE_V) {
                /* Lower the rail only after the lower clock is reached. */
                tuning_set_active(true, generation);
                if (!frequency_apply_voltage(target_voltage_v, generation)) {
                    tuning_set_active(false, generation);
                    return BONANZA_POWER_MANAGEMENT_stop_requested();
                }
                return true;
            }
            tuning_set_active(false, generation);
            return true;
        }
        tuning_set_active(true, generation);

        if (target_mhz > BZM_FREQUENCY_POWER_ON_MHZ + 0.001f &&
            frequency_domains_all_at(
                &state, BZM_FREQUENCY_POWER_ON_MHZ)) {
            const float initial_mhz =
                bzm_frequency_initial_mhz(target_mhz);
            if (initial_mhz >
                BZM_FREQUENCY_POWER_ON_MHZ + 0.001f) {
                float initial[BZM_BRINGUP_ASIC_COUNT]
                             [BZM_BRINGUP_PLL_COUNT];
                for (size_t asic = 0;
                     asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
                    for (size_t pll = 0;
                         pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
                        initial[asic][pll] = initial_mhz;
                    }
                }
                float actual_mhz = state.clock_mhz;
                if (!frequency_apply_domains(
                        initial, true, generation, &actual_mhz)) {
                    tuning_set_active(false, generation);
                    return BONANZA_POWER_MANAGEMENT_stop_requested();
                }
                ESP_LOGI(TAG,
                         "BZM live shortcut reached %.3f MHz; continuing "
                         "directly to the user target",
                         initial_mhz);
                uint32_t completed = 0;
                if (!BZM_work_replacement_snapshot(&RUNTIME.replacement_generation,
                        &completed, &RUNTIME.replacement_pending)) return false;
                RUNTIME.replacement_started_ms = now_ms();
                return true;
            }
        }

        float next[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT];
        frequency_domains_copy(next, state.domain_clock_mhz);
        bool changing = false;
        for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
            for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
                const float current = state.domain_clock_mhz[asic][pll];
                if (current > target_mhz + 0.001f) {
                    next[asic][pll] =
                        fmaxf(target_mhz,
                              current - BZM_FREQUENCY_RAMP_STEP_MHZ);
                    changing = true;
                } else if (current < target_mhz - 0.001f) {
                    next[asic][pll] =
                        fminf(target_mhz,
                              current + BZM_FREQUENCY_RAMP_STEP_MHZ);
                    changing = true;
                }
            }
        }
        if (!changing) {
            tuning_set_active(false, generation);
            return true;
        }

        float actual_mhz = state.clock_mhz;
        if (!frequency_apply_domains(
                next, false, generation, &actual_mhz)) {
            tuning_set_active(false, generation);
            return BONANZA_POWER_MANAGEMENT_stop_requested();
        }
        uint32_t completed = 0;
        if (!BZM_work_replacement_snapshot(&RUNTIME.replacement_generation,
                &completed, &RUNTIME.replacement_pending)) return false;
        RUNTIME.replacement_started_ms = now_ms();
    }
    return true;
}

static bool recoverable_overheat_fault(bzm_runtime_health_fault_t fault)
{
    return fault == BZM_RUNTIME_HEALTH_FAULT_ASIC_OVERHEAT ||
           fault == BZM_RUNTIME_HEALTH_FAULT_TPS_OVERHEAT;
}

static void board_io_task(void *parameter)
{
    (void)parameter;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BZM_MONITOR_PERIOD_MS));
        pthread_mutex_lock(&RUNTIME.lock);
        const uint64_t current_ms = now_ms();
        if (RUNTIME.running) {
            /* No pool/socket calls, lifecycle transitions, NVS writes, or
             * tuning here. This reader outlives stalled share submission. */
            (void)BZM_poll(1);
            if (RUNTIME.health.status == BZM_RUNTIME_HEALTH_BAD ||
                RUNTIME.watchdog_deadline_ms <= current_ms) {
                close_dispatch_locked();
                pthread_mutex_unlock(&RUNTIME.lock);
                continue; /* Do not extend an unhealthy board's output lease. */
            }
            bzm_bridge_safety_status_t status = {0};
            if (BZM_bridge_safety_heartbeat(&status) != ESP_OK ||
                !bridge_status_runtime_good(&status) ||
                status.state != BZM_BRIDGE_SAFETY_STATE_CONTROLLED ||
                status.lease_remaining_ms == 0) {
                RUNTIME.health = (bzm_runtime_health_result_t){
                    .status = BZM_RUNTIME_HEALTH_BAD,
                    .fault = BZM_RUNTIME_HEALTH_FAULT_BRIDGE_UNAVAILABLE,
                };
                snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail),
                         "bridge heartbeat/status interlock failed");
            } else {
                RUNTIME.bridge_status = status;
                RUNTIME.bridge_status_valid = true;
            }
            if (RUNTIME.health.status != BZM_RUNTIME_HEALTH_BAD &&
                (RUNTIME.health_sampled_at_ms == 0 ||
                 current_ms - RUNTIME.health_sampled_at_ms >= BZM_HEALTH_PERIOD_MS)) {
                (void)sample_runtime_health_locked();
            }
            if (RUNTIME.health.status == BZM_RUNTIME_HEALTH_BAD) {
                /* Revoking work is an interlock; only power management
                 * executes the shutdown or decides whether to recover. */
                close_dispatch_locked();
            }
        }
        pthread_mutex_unlock(&RUNTIME.lock);
    }
}

esp_err_t BZM_board_init(GlobalState * global_state)
{
    if (global_state == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!(global_state->DEVICE_CONFIG.family.id == BONANZA) || global_state->DEVICE_CONFIG.family.asic.id != BZM) {
        return ESP_OK;
    }
    if (!BZM_driver_state_init(global_state) ||
        !runtime_state_init(global_state)) {
        return ESP_ERR_NO_MEM;
    }

    pthread_mutex_lock(&RUNTIME.lock);
    if (RUNTIME.initialized) {
        pthread_mutex_unlock(&RUNTIME.lock);
        return ESP_OK;
    }
    RUNTIME.global_state = global_state;
    bzm_frequency_target_t configured_target;
    const float requested_mhz =
        (RUNTIME.global_state->SELF_TEST_MODULE.is_active ? 800.0f : nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY));
    if (!bzm_frequency_request_is_valid(requested_mhz) ||
        !bzm_frequency_resolve_target(
            requested_mhz, &configured_target)) {
        configured_target = (bzm_frequency_target_t){
            .requested_mhz = BZM_FREQUENCY_POWER_ON_MHZ,
            .actual_mhz = BZM_FREQUENCY_POWER_ON_MHZ,
        };
        ESP_LOGW(TAG,
                 "Invalid saved Bonanza frequency %.3f MHz; using 800 MHz",
                 requested_mhz);
    }
    RUNTIME.frequency_target_mhz = configured_target.actual_mhz;
    const uint16_t requested_voltage_mv =
        (RUNTIME.global_state->SELF_TEST_MODULE.is_active ? 2800 : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE));
    float requested_voltage_v = 0.0f;
    if (bzm_power_resolve_user_voltage(
            requested_voltage_mv, &requested_voltage_v)) {
        RUNTIME.voltage_target_v = requested_voltage_v;
    } else {
        RUNTIME.voltage_target_v = BZM_TPS546_FIXED_VOUT_V;
        ESP_LOGW(TAG,
                 "Invalid saved Bonanza rail voltage %u mV; using 2800 mV",
                 (unsigned)requested_voltage_mv);
    }
    RUNTIME.frequency_target_generation = 1;
    RUNTIME.frequency_ramp_active =
        fabsf(configured_target.actual_mhz -
              BZM_FREQUENCY_POWER_ON_MHZ) >= 0.001f ||
        fabsf(RUNTIME.voltage_target_v -
              BZM_TPS546_FIXED_VOUT_V) >= 0.001f;
    RUNTIME.rail_command_v = BZM_TPS546_FIXED_VOUT_V;
    global_state->POWER_MANAGEMENT_MODULE.frequency_value =
        configured_target.actual_mhz;
    global_state->POWER_MANAGEMENT_MODULE.actual_frequency = 0.0f;
    global_state->POWER_MANAGEMENT_MODULE.expected_hashrate =
        expected_bzm_hashrate_ghs(
            global_state, configured_target.actual_mhz);
    bzm_ch2_confirmation_init(&RUNTIME.ch2_confirmation);
    bzm_pll_lock_confirmation_init(&RUNTIME.pll_lock_confirmation);
    atomic_init(&RUNTIME.dispatch_enabled, false);
    atomic_init(&RUNTIME.pause_requested, false);
    atomic_init(&RUNTIME.dispatch_deadline_ms, 0);
    atomic_init(&RUNTIME.execution_deadline_ms, 0);
    atomic_init(&RUNTIME.execution_cancelled, false);
    BZM_set_operation_authorizer(runtime_execution_authorizer, &RUNTIME);

    RUNTIME.initialized = true;
    bool safe = shutdown_locked();
    pthread_mutex_unlock(&RUNTIME.lock);
    if (!safe)
        return ESP_FAIL;

    if (xTaskCreate(board_io_task, "bzm_io", 6144, NULL,
                    BZM_IO_TASK_PRIORITY, NULL) != pdPASS) {
        pthread_mutex_lock(&RUNTIME.lock);
        close_dispatch_locked();
        (void)latch_fault_locked("Bonanza safety monitor task could not start");
        pthread_mutex_unlock(&RUNTIME.lock);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Bonanza board operations initialized at safe-off");
    return ESP_OK;
}

bool BZM_board_stop(void *context)
{
    (void)context;
    if (RUNTIME_STATE == NULL) return false;
    /* Publish intent before waiting for either the board lock or the
     * BZM reactor. A live PLL/rail transaction that completes concurrently
     * must yield to pause instead of latching its cancellation as a fault. */
    atomic_store_explicit(&RUNTIME.pause_requested, true,
                          memory_order_release);
    pthread_mutex_lock(&RUNTIME.lock);
    if (!RUNTIME.initialized || RUNTIME.global_state == NULL) {
        pthread_mutex_unlock(&RUNTIME.lock);
        return false;
    }

    close_dispatch_locked();
    RUNTIME.frequency_ramp_active = false;
    ++RUNTIME.frequency_target_generation;

    bool safe = RUNTIME.safe_off || shutdown_locked();

    RUNTIME.global_state->ASIC_initalized = false;
    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.actual_frequency = 0.0f;
    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.expected_hashrate = 0.0f;
    if (safe) {
        RUNTIME.rail_command_v = BZM_TPS546_FIXED_VOUT_V;
        RUNTIME.global_state->POWER_MANAGEMENT_MODULE.core_voltage = 0.0f;
        RUNTIME.global_state->POWER_MANAGEMENT_MODULE.current = 0.0f;
        RUNTIME.global_state->POWER_MANAGEMENT_MODULE.power = 0.0f;
    }
    sync_dispatch_locked();
    pthread_mutex_unlock(&RUNTIME.lock);

    if (safe) {
        ESP_LOGI(TAG, "Bonanza mining paused at verified shutdown");
    } else {
        ESP_LOGE(TAG,
                 "Bonanza mining pause failed closed without verified shutdown");
    }
    return safe;
}

static bool board_start(void *context)
{
    (void)context;
    if (RUNTIME_STATE == NULL) return false;
    pthread_mutex_lock(&RUNTIME.lock);
    if (!RUNTIME.initialized || RUNTIME.global_state == NULL) {
        pthread_mutex_unlock(&RUNTIME.lock);
        return false;
    }
    if (RUNTIME.running) {
        bool healthy = dispatch_allowed_locked(now_ms()) && !BZM_has_io_fault();
        if (healthy) atomic_store_explicit(&RUNTIME.pause_requested, false, memory_order_release);
        pthread_mutex_unlock(&RUNTIME.lock);
        return healthy;
    }
    if (RUNTIME.fault_latched && shutdown_locked()) {
        RUNTIME.fault_latched = false;
        RUNTIME.fault_detail[0] = '\0';
    }
    if (RUNTIME.fault_latched || !RUNTIME.safe_off) {
        pthread_mutex_unlock(&RUNTIME.lock);
        return false;
    }
    RUNTIME.health = (bzm_runtime_health_result_t){0};
    atomic_store_explicit(&RUNTIME.execution_cancelled, false, memory_order_release);
    RUNTIME.replacement_pending = false;

    bzm_frequency_target_t frequency_target;
    float voltage_target_v = 0.0f;
    if (!configured_tuning_target(
            &frequency_target, &voltage_target_v)) {
        pthread_mutex_unlock(&RUNTIME.lock);
        ESP_LOGE(TAG,
                 "Bonanza resume rejected invalid saved frequency or voltage");
        return false;
    }

    RUNTIME.frequency_target_mhz = frequency_target.actual_mhz;
    RUNTIME.voltage_target_v = voltage_target_v;
    ++RUNTIME.frequency_target_generation;
    RUNTIME.frequency_ramp_active =
        fabsf(frequency_target.actual_mhz -
              BZM_FREQUENCY_POWER_ON_MHZ) >= 0.001f ||
        fabsf(voltage_target_v - BZM_TPS546_FIXED_VOUT_V) >= 0.001f;
    RUNTIME.rail_command_v = BZM_TPS546_FIXED_VOUT_V;
    RUNTIME.health_sampled_at_ms = 0;
    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.frequency_value =
        frequency_target.actual_mhz;
    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.actual_frequency = 0.0f;
    RUNTIME.global_state->POWER_MANAGEMENT_MODULE.expected_hashrate =
        expected_bzm_hashrate_ghs(
            RUNTIME.global_state, frequency_target.actual_mhz);

    atomic_store_explicit(&RUNTIME.pause_requested, false,
                          memory_order_release);
    const bool started = start_production_mining_locked();
    if (!started) {
        atomic_store_explicit(&RUNTIME.pause_requested, true,
                              memory_order_release);
        RUNTIME.frequency_ramp_active = false;
        RUNTIME.global_state->POWER_MANAGEMENT_MODULE.actual_frequency =
            0.0f;
        RUNTIME.global_state->POWER_MANAGEMENT_MODULE.expected_hashrate =
            0.0f;
    }
    pthread_mutex_unlock(&RUNTIME.lock);

    if (started) {
        ESP_LOGI(TAG,
                 "Bonanza mining resumed at 800 MHz; live target %.3f MHz",
                 frequency_target.actual_mhz);
    } else {
        ESP_LOGE(TAG, "Bonanza mining resume failed closed");
    }
    return started;
}

bool BZM_board_maintenance(void *context, bonanza_power_owner_t owner, bool acquire)
{
    (void)context;
    if (RUNTIME_STATE == NULL ||
        (owner != BONANZA_POWER_OWNER_OTA && owner != BONANZA_POWER_OWNER_RESTART)) return false;
    /* The power policy is the sole maintenance owner. Every transition still
     * performs a fresh physical shutdown before handing control to OTA/reset. */
    atomic_store_explicit(&RUNTIME.pause_requested, true, memory_order_release);
    pthread_mutex_lock(&RUNTIME.lock);
    bool ok = RUNTIME.initialized && shutdown_locked();
    if (ok && acquire) {
        RUNTIME.fault_latched = false;
        RUNTIME.fault_detail[0] = '\0';
    }
    pthread_mutex_unlock(&RUNTIME.lock);
    return ok;
}

bonanza_power_sample_t BZM_board_sample(void)
{
    bonanza_power_sample_t result = {.health = BONANZA_POWER_HEALTH_FAULT};
    if (RUNTIME_STATE == NULL || !RUNTIME.initialized) return result;
    pthread_mutex_lock(&RUNTIME.lock);
    const uint64_t current_ms = now_ms();
    if (RUNTIME.running) {
        if (!bzm_lease_guard_deadline_allows(RUNTIME.watchdog_deadline_ms, current_ms) ||
            !bzm_lease_guard_make_deadline(current_ms, BZM_BOARD_WATCHDOG_MS, &RUNTIME.watchdog_deadline_ms)) {
            RUNTIME.health.status = BZM_RUNTIME_HEALTH_BAD;
            RUNTIME.health.fault = BZM_RUNTIME_HEALTH_FAULT_BRIDGE_LEASE;
            snprintf(RUNTIME.health.detail, sizeof(RUNTIME.health.detail), "power-management watchdog expired");
        }
    }
    result.health = RUNTIME.fault_latched ? BONANZA_POWER_HEALTH_FAULT : BONANZA_POWER_HEALTH_OK;
    if (RUNTIME.health.status == BZM_RUNTIME_HEALTH_BAD) {
        result.health = recoverable_overheat_fault(RUNTIME.health.fault)
            ? BONANZA_POWER_HEALTH_OVERHEAT : BONANZA_POWER_HEALTH_FAULT;
    }
    snprintf(result.detail, sizeof(result.detail), "%s",
        RUNTIME.health.status == BZM_RUNTIME_HEALTH_BAD ? RUNTIME.health.detail :
        RUNTIME.fault_latched ? RUNTIME.fault_detail : "");
    BONANZA_TPS546_StatusSnapshot power = {0};
    bool pgood = true;
    result.vreg_valid = BONANZA_VCORE_bzm_snapshot(&power, &pgood) == ESP_OK;
    result.vreg_c = power.read_temp1;
    if (result.vreg_valid && RUNTIME.global_state != NULL)
        RUNTIME.global_state->POWER_MANAGEMENT_MODULE.vr_temp = result.vreg_c;
    sync_dispatch_locked();
    pthread_mutex_unlock(&RUNTIME.lock);
    return result;
}

bonanza_power_start_result_t BZM_board_start(void *context)
{
    if (board_start(context)) return BONANZA_POWER_START_OK;
    if (RUNTIME_STATE == NULL) return BONANZA_POWER_START_FAILED;
    pthread_mutex_lock(&RUNTIME.lock);
    bool hot = RUNTIME.health.status == BZM_RUNTIME_HEALTH_BAD &&
               recoverable_overheat_fault(RUNTIME.health.fault);
    pthread_mutex_unlock(&RUNTIME.lock);
    return hot ? BONANZA_POWER_START_OVERHEAT : BONANZA_POWER_START_FAILED;
}
