#include "bonanza_vcore.h"
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
static esp_err_t bonanza_set_5v_enabled(void *context, bool enabled)
{
    (void)context;
    return BZM_control_set_5v_enabled(enabled);
}

static esp_err_t bonanza_set_regulator_enabled(void *context, bool enabled)
{
    (void)context;
    return BZM_control_set_core_enabled(enabled);
}

static esp_err_t bonanza_set_vout(void *context, float volts)
{
    GlobalState *state = context;
    if (volts != 0.0f &&
        !bzm_power_runtime_voltage_is_allowed(volts)) {
        ESP_LOGE(TAG,
                 "Bonanza TPS rail request %.3fV is outside 2.1..3.2V",
                 volts);
        return ESP_ERR_INVALID_ARG;
    }
    return state->DEVICE_CONFIG.TPS546 ? TPS546_set_vout(volts)
                                       : ESP_ERR_INVALID_STATE;
}

static esp_err_t bonanza_validate_power(void *context, float expected_vout)
{
    GlobalState *state = context;
    if (!bzm_power_runtime_voltage_is_allowed(expected_vout) ||
        gpio_get_level(state->DEVICE_CONFIG.pins.bzm->pgood) == 0) {
        ESP_LOGE(TAG, "TPS546 PGOOD remained low");
        return ESP_FAIL;
    }

    TPS546_StatusSnapshot snapshot;
    ESP_RETURN_ON_ERROR(TPS546_snapshot_status(&snapshot), TAG,
                        "TPS546 status/telemetry read failed");
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
        return ESP_FAIL;
    }
    return ESP_OK;
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
static bool rail_enabled;

/* Lifecycle helpers run with voltage_lock held. Startup always uses 2.8 V. */
static esp_err_t prepare_startup_power(GlobalState *state)
{
    if (rail_enabled) return ESP_OK;
    bzm_control_status_t status;
    esp_err_t err = BZM_control_safe_off(&status);
    if (err == ESP_OK) err = BZM_control_clear_safety_fault(&status);
    if (err == ESP_OK) err = bzm_power_set_rail_enabled(&BONANZA_POWER_OPS, state, true);
    if (err == ESP_OK) err = BZM_control_set_5v_enabled(true);
    rail_enabled = err == ESP_OK;
    return err;
}

static esp_err_t apply_runtime_voltage(GlobalState *state, float volts)
{
    return bzm_power_set_runtime_voltage(&BONANZA_POWER_OPS, state, volts);
}

static esp_err_t shutdown_power(GlobalState *state)
{
    bool reset = BZM_hold_reset();
    esp_err_t err = bzm_power_set_rail_enabled(&BONANZA_POWER_OPS, state, false);
    rail_enabled = false;
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
            state->SYSTEM_MODULE.hardware_fault = true;
            snprintf(state->SYSTEM_MODULE.hardware_fault_msg,
                     sizeof(state->SYSTEM_MODULE.hardware_fault_msg), "BZM rail configuration failed");
        }
    }
    pthread_mutex_unlock(&voltage_lock);
    return err;
}
