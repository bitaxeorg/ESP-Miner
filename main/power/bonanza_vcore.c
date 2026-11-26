#include "bonanza_vcore.h"
#include "vcore.h"
#include <math.h>
#include <stdio.h>
#include <pthread.h>
#include "bzm/driver.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bzm/control.h"
#include "bzm/power.h"
#include "global_state.h"

const TPS546_CONFIG TPS546_CONFIG_BONANZA_1004 = {
    .TPS546_INIT_PHASE = TPS546_INIT_PHASE_MULTI,
    .TPS546_INIT_VIN_ON = 11.0f,
    .TPS546_INIT_VIN_OFF = 10.5f,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0f,
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0f,
    .TPS546_INIT_SCALE_LOOP = 0.125f,
    .TPS546_INIT_VOUT_MIN = 2.1f,
    .TPS546_INIT_VOUT_MAX = 3.2f,
    .TPS546_INIT_VOUT_COMMAND = 2.8f,
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 50.0f,
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 55.0f,
    .TPS546_INIT_STACK_CONFIG = TPS546_SINGLE_PHASE_STACK_CONFIG,
    .TPS546_INIT_SYNC_CONFIG = 0x10,
    .TPS546_INIT_FREQUENCY = 325,
    .TPS546_INIT_COMPENSATION_CONFIG = {0x13, 0x11, 0x8c, 0x1d, 0x08},
    .TPS546_USE_ENABLE_PIN = true, /* The trip handler removes core power through EN/UVLO. */
    .TPS546_CHECK_STARTUP_WRITES = true,
    .TPS546_VOUT_BOUNDS_FIRST = true, /* Floating VSEL can start with a 1.5 V maximum. */
};

static const char *TAG = "bonanza_vcore";
static float minimum_input_voltage;
esp_err_t BONANZA_VCORE_init(GlobalState *GLOBAL_STATE, TPS546_CONFIG config)
{
    ESP_RETURN_ON_ERROR(BZM_control_init(GLOBAL_STATE->DEVICE_CONFIG.board_version, GLOBAL_STATE->DEVICE_CONFIG.pins.bzm),
                        TAG, "board control initialization failed");
    ESP_RETURN_ON_ERROR(BZM_control_set_5v_enabled(false), TAG, "5 V safe-state failed");
    minimum_input_voltage = config.TPS546_INIT_VIN_OFF;
    return TPS546_init(config);
}
static bzm_status_t bonanza_esp_to_bzm(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return BZM_OK;
    case ESP_ERR_INVALID_ARG: return BZM_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_STATE: return BZM_ERR_INVALID_STATE;
    case ESP_ERR_NOT_SUPPORTED: return BZM_ERR_NOT_SUPPORTED;
    case ESP_ERR_NO_MEM: return BZM_ERR_NO_MEM;
    case ESP_ERR_TIMEOUT: return BZM_ERR_TIMEOUT;
    default: return BZM_ERR_IO;
    }
}

static esp_err_t bonanza_bzm_to_esp(bzm_status_t status)
{
    switch (status) {
    case BZM_OK: return ESP_OK;
    case BZM_ERR_INVALID_ARG: return ESP_ERR_INVALID_ARG;
    case BZM_ERR_INVALID_STATE: return ESP_ERR_INVALID_STATE;
    case BZM_ERR_NOT_SUPPORTED: return ESP_ERR_NOT_SUPPORTED;
    case BZM_ERR_NO_MEM: return ESP_ERR_NO_MEM;
    case BZM_ERR_TIMEOUT: return ESP_ERR_TIMEOUT;
    default: return ESP_FAIL;
    }
}

static bzm_status_t bonanza_set_5v_enabled(void *context, bool enabled)
{
    (void)context;
    return bonanza_esp_to_bzm(BZM_control_set_5v_enabled(enabled));
}

static bzm_status_t bonanza_set_regulator_enabled(void *context, bool enabled)
{
    (void)context;
    return bonanza_esp_to_bzm(BZM_control_set_core_enabled(enabled));
}

static bzm_status_t bonanza_set_vout(void *context, float volts)
{
    GlobalState *state = context;
    if (volts != 0.0f &&
        !bzm_power_runtime_voltage_is_allowed(volts)) {
        ESP_LOGE(TAG,
                 "Bonanza TPS rail request %.3fV is outside 2.1..3.2V",
                 volts);
        return BZM_ERR_INVALID_ARG;
    }
    return bonanza_esp_to_bzm(state->DEVICE_CONFIG.TPS546 ? TPS546_set_vout(volts)
                                                          : ESP_ERR_INVALID_STATE);
}

static bzm_status_t bonanza_validate_power(void *context, float expected_vout)
{
    GlobalState *state = context;
    if (!bzm_power_runtime_voltage_is_allowed(expected_vout) ||
        gpio_get_level(state->DEVICE_CONFIG.pins.bzm->core_power_good) == 0) {
        ESP_LOGE(TAG, "TPS546 PGOOD remained low");
        return BZM_ERR_IO;
    }

    TPS546_StatusSnapshot snapshot;
    esp_err_t snap_err = TPS546_snapshot_status(&snapshot);
    if (snap_err != ESP_OK) {
        ESP_LOGE(TAG, "TPS546 status/telemetry read failed");
        return bonanza_esp_to_bzm(snap_err);
    }
    if ((snapshot.status_word &
         (TPS546_STATUS_VOUT | TPS546_STATUS_IOUT |
          TPS546_STATUS_INPUT | TPS546_STATUS_PGOOD |
          TPS546_STATUS_OFF | TPS546_STATUS_TEMP |
          TPS546_STATUS_CML)) != 0 ||
        (snapshot.operation & 0x80) == 0 ||
        !isfinite(snapshot.vout_command) ||
        fabsf(snapshot.vout_command - expected_vout) >
            BZM_TPS546_VOUT_READBACK_TOLERANCE_V ||
        snapshot.read_vin < minimum_input_voltage ||
        snapshot.read_vout <
            expected_vout - BZM_TPS546_VOUT_OPERATING_TOLERANCE_V ||
        snapshot.read_vout >
            expected_vout + BZM_TPS546_VOUT_OPERATING_TOLERANCE_V ||
        snapshot.read_temp1 >= TPS546_INIT_OT_FAULT_LIMIT) {
        TPS546_log_snapshot(&snapshot);
        ESP_LOGE(TAG,
                 "TPS546 %.3fV command/status/telemetry validation failed",
                 expected_vout);
        return BZM_ERR_IO;
    }
    return BZM_OK;
}

static void bonanza_power_delay(void *context, uint32_t delay_ms)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static const bzm_power_ops_t BONANZA_POWER_OPS = {
    .set_5v_enabled = bonanza_set_5v_enabled,
    .set_regulator_enabled = bonanza_set_regulator_enabled,
    .set_vout = bonanza_set_vout,
    .validate_power = bonanza_validate_power,
    .delay_ms = bonanza_power_delay,
};

/* Only the regulator calls need serialization; mining policy stays in the shared task. */
static pthread_mutex_t voltage_lock = PTHREAD_MUTEX_INITIALIZER;

static esp_err_t wait_pgood_low(const GlobalState *state)
{
    const unsigned max_wait_ms = 200;
    const unsigned step_ms = 10;
    for (unsigned waited = 0; waited <= max_wait_ms; waited += step_ms) {
        if (gpio_get_level(state->DEVICE_CONFIG.pins.bzm->core_power_good) == 0) {
            return ESP_OK;
        }
        if (waited < max_wait_ms) {
            vTaskDelay(pdMS_TO_TICKS(step_ms));
        }
    }
    ESP_LOGE(TAG, "TPS546 PGOOD did not discharge low after safe-off");
    return ESP_ERR_TIMEOUT;
}

/* Lifecycle helpers run with voltage_lock held. Startup always uses 2.8 V. */
static esp_err_t prepare_startup_power(GlobalState *state)
{
    bzm_control_status_t status;
    esp_err_t err = BZM_control_get_safety_status(&status);
    if (err != ESP_OK) return err;
    if (!status.valid || status.fault != BZM_CONTROL_FAULT_NONE || state->SYSTEM_MODULE.hardware_fault)
        return ESP_ERR_INVALID_STATE;
    if (status.core_enabled && status.five_volt_enabled) return ESP_OK;

    // An active chain with a missing rail must go through shutdown before startup.
    if (state->ASIC_initalized) return ESP_ERR_INVALID_STATE;
    err = BZM_control_safe_off(&status);
    if (err == ESP_OK && (!status.valid || status.fault != BZM_CONTROL_FAULT_NONE))
        return ESP_ERR_INVALID_STATE;
    /* Command OPERATION_OFF before re-enabling EN/UVLO so an earlier runtime
     * voltage (up to 3.2 V) cannot be briefly applied before 2.8 V is set. */
    if (err == ESP_OK && state->DEVICE_CONFIG.TPS546) err = TPS546_set_vout(0.0f);
    if (err == ESP_OK) err = wait_pgood_low(state);
    if (err == ESP_OK) err = BZM_control_clear_safety_fault(&status);
    /* Clear historical latched PMBus warnings (such as OT_WARN after overheat
     * recovery) before enabling and validating the startup rail. */
    if (err == ESP_OK && state->DEVICE_CONFIG.TPS546) err = TPS546_clear_faults();
    if (err == ESP_OK) err = bonanza_bzm_to_esp(bzm_power_set_rail_enabled(&BONANZA_POWER_OPS, state, true));
    if (err == ESP_OK) err = BZM_control_set_5v_enabled(true);
    return err;
}

static esp_err_t apply_runtime_voltage(GlobalState *state, float volts)
{
    if (state->DEVICE_CONFIG.TPS546) {
        esp_err_t err = TPS546_clear_faults();
        if (err != ESP_OK) return err;
    }
    return bonanza_bzm_to_esp(bzm_power_set_runtime_voltage(&BONANZA_POWER_OPS, state, volts));
}

static esp_err_t shutdown_power(GlobalState *state)
{
    bool reset = BZM_hold_reset();
    esp_err_t err = bonanza_bzm_to_esp(bzm_power_set_rail_enabled(&BONANZA_POWER_OPS, state, false));
    return err == ESP_OK && !reset ? ESP_FAIL : err;
}

esp_err_t BONANZA_VCORE_set_voltage(GlobalState *state, float volts)
{
    if (volts != 0.0f && !bzm_power_runtime_voltage_is_allowed(volts)) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&voltage_lock);
    esp_err_t err = ESP_OK;
    bzm_bringup_state_t asic;
    if (volts == 0.0f) {
        err = shutdown_power(state);
    } else {
        err = prepare_startup_power(state);
        // The caller reapplies its request after ASIC initialization at 800 MHz.
        if (err == ESP_OK && BZM_get_state(&asic) && asic.running) {
            err = apply_runtime_voltage(state, volts);
        }
        if (err != ESP_OK) {
            (void)shutdown_power(state);
            if (!state->SYSTEM_MODULE.hardware_fault) {
                state->SYSTEM_MODULE.hardware_fault = true;
                snprintf(state->SYSTEM_MODULE.hardware_fault_msg,
                         sizeof(state->SYSTEM_MODULE.hardware_fault_msg), "BZM rail configuration failed");
            }
        }
    }
    pthread_mutex_unlock(&voltage_lock);
    return err;
}

const VcoreDriver BONANZA_VCORE_DRIVER = {
    .init = BONANZA_VCORE_init,
    .set_voltage = BONANZA_VCORE_set_voltage,
    .asserts_reset_on_shutdown = true,
};
