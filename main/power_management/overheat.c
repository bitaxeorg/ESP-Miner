/**
 * @file overheat.c
 * @brief Overheat detection and recovery implementation
 *
 * Consolidates the overheat handling logic that was duplicated
 * across multiple device type cases in power_management_task.c.
 */

#include "overheat.h"
#include <stdio.h>
#include <string.h>

/* Constants for recovery */
#define SOFT_RECOVERY_COOLDOWN_MS  300000  /* 5 minutes */
#define RECOVERY_CHECK_INTERVAL_MS 1000    /* 1 second */

/* NVS key names - these must match nvs_config.h */
#define NVS_KEY_OVERHEAT_COUNT    "overheatCount"
#define NVS_KEY_ASIC_VOLTAGE      "asicvoltage"
#define NVS_KEY_ASIC_FREQ         "asicfrequency"
#define NVS_KEY_FAN_SPEED         "fanspeed"
#define NVS_KEY_AUTO_FAN_SPEED    "autofanspeed"
#define NVS_KEY_OVERHEAT_MODE     "overheatMode"

/* Device model values - must match global_state.h */
#define DEVICE_MODEL_MAX    0
#define DEVICE_MODEL_ULTRA  1
#define DEVICE_MODEL_SUPRA  2
#define DEVICE_MODEL_GAMMA  3

/* ============================================
 * Pure Detection Function
 * ============================================ */

overheat_check_result_t overheat_check(const overheat_check_input_t* input,
                                        uint16_t overheat_count)
{
    overheat_check_result_t result = {
        .should_trigger = false,
        .overheat_type = PM_OVERHEAT_NONE,
        .severity = PM_SEVERITY_NONE
    };

    if (!input) {
        return result;
    }

    /* Use pure function to check if overheat should trigger */
    result.should_trigger = pm_should_trigger_overheat(
        input->chip_temp,
        input->vr_temp,
        input->frequency,
        input->voltage
    );

    if (!result.should_trigger) {
        return result;
    }

    /* Determine overheat type */
    result.overheat_type = pm_check_overheat(
        input->chip_temp,
        input->vr_temp,
        PM_THROTTLE_TEMP,
        PM_TPS546_THROTTLE_TEMP
    );

    /* Determine severity based on count */
    result.severity = pm_calc_overheat_severity(overheat_count);

    return result;
}

/* ============================================
 * Logging Helpers
 * ============================================ */

int overheat_format_log_data(char* buf, size_t buf_size,
                              const overheat_check_input_t* input,
                              const char* device_name)
{
    if (!buf || buf_size == 0 || !input) {
        return 0;
    }

    if (input->vr_temp > 0.0f) {
        return snprintf(buf, buf_size,
            "{\"vrTemp\":%.1f,\"chipTemp\":%.1f,\"vrThreshold\":%.1f,\"chipThreshold\":%.1f,\"device\":\"%s\"}",
            input->vr_temp, input->chip_temp,
            PM_TPS546_THROTTLE_TEMP, PM_THROTTLE_TEMP,
            device_name ? device_name : "UNKNOWN");
    } else {
        return snprintf(buf, buf_size,
            "{\"chipTemp\":%.1f,\"threshold\":%.1f,\"device\":\"%s\"}",
            input->chip_temp, PM_THROTTLE_TEMP,
            device_name ? device_name : "UNKNOWN");
    }
}

int overheat_format_device_info(char* buf, size_t buf_size,
                                 const overheat_check_input_t* input,
                                 const char* device_name)
{
    if (!buf || buf_size == 0 || !input) {
        return 0;
    }

    if (input->vr_temp > 0.0f) {
        return snprintf(buf, buf_size, "%s VR: %.1fC ASIC %.1fC",
                        device_name ? device_name : "UNKNOWN",
                        input->vr_temp, input->chip_temp);
    } else {
        return snprintf(buf, buf_size, "%s ASIC %.1fC",
                        device_name ? device_name : "UNKNOWN",
                        input->chip_temp);
    }
}

/* ============================================
 * Recovery Execution
 * ============================================ */

/**
 * @brief Disable ASIC power based on device type
 */
static void disable_asic_power(const overheat_device_config_t* config,
                                const overheat_hw_ops_t* hw_ops,
                                void* hw_ctx)
{
    if (!config || !hw_ops) {
        return;
    }

    switch (config->device_model) {
        case DEVICE_MODEL_MAX:
            if (config->has_power_en && hw_ops->set_asic_enable) {
                hw_ops->set_asic_enable(1);  /* 1 = disabled */
            }
            break;

        case DEVICE_MODEL_ULTRA:
        case DEVICE_MODEL_SUPRA:
            if (config->board_version >= 402 && config->board_version <= 499) {
                /* TPS546 boards - set voltage to 0 */
                if (hw_ops->set_vcore) {
                    hw_ops->set_vcore(0.0f, hw_ctx);
                }
            } else if (config->has_power_en && hw_ops->set_asic_enable) {
                hw_ops->set_asic_enable(1);
            }
            break;

        case DEVICE_MODEL_GAMMA:
            if (hw_ops->set_vcore) {
                hw_ops->set_vcore(0.0f, hw_ctx);
            }
            break;

        default:
            break;
    }
}

/**
 * @brief Apply safe NVS values
 */
static void apply_safe_values(const overheat_safe_values_t* values,
                               const overheat_hw_ops_t* hw_ops)
{
    if (!hw_ops || !hw_ops->nvs_set_u16) {
        return;
    }

    overheat_safe_values_t safe = OVERHEAT_SAFE_VALUES_DEFAULT;
    if (values) {
        safe = *values;
    }

    hw_ops->nvs_set_u16(NVS_KEY_ASIC_VOLTAGE, safe.voltage_mv);
    hw_ops->nvs_set_u16(NVS_KEY_ASIC_FREQ, safe.frequency_mhz);
    hw_ops->nvs_set_u16(NVS_KEY_FAN_SPEED, safe.fan_speed_pct);
    hw_ops->nvs_set_u16(NVS_KEY_AUTO_FAN_SPEED, safe.auto_fan_off ? 0 : 1);
    hw_ops->nvs_set_u16(NVS_KEY_OVERHEAT_MODE, 1);
}

/**
 * @brief Get overheat type string for logging
 */
static const char* overheat_type_string(pm_overheat_type_t type)
{
    switch (type) {
        case PM_OVERHEAT_CHIP: return "ASIC";
        case PM_OVERHEAT_VR:   return "VR";
        case PM_OVERHEAT_BOTH: return "ASIC+VR";
        default:               return "Unknown";
    }
}

void overheat_execute_recovery(pm_overheat_severity_t severity,
                                const overheat_device_config_t* device_config,
                                const overheat_safe_values_t* safe_values,
                                const overheat_hw_ops_t* hw_ops,
                                void* hw_ctx,
                                pm_overheat_type_t overheat_type,
                                const char* log_json_extra)
{
    if (!hw_ops || severity == PM_SEVERITY_NONE) {
        return;
    }

    /* Step 1: Increment and get overheat count */
    uint16_t overheat_count = 0;
    if (hw_ops->nvs_get_u16 && hw_ops->nvs_set_u16) {
        overheat_count = hw_ops->nvs_get_u16(NVS_KEY_OVERHEAT_COUNT, 0);
        overheat_count++;
        hw_ops->nvs_set_u16(NVS_KEY_OVERHEAT_COUNT, overheat_count);
    }

    /* Step 2: Set fan to maximum */
    if (hw_ops->set_fan_speed) {
        hw_ops->set_fan_speed(1.0f);
    }

    /* Step 3: Disable ASIC power */
    disable_asic_power(device_config, hw_ops, hw_ctx);

    /* Step 4: Apply safe NVS values */
    apply_safe_values(safe_values, hw_ops);

    /* Step 5: Log the event */
    if (hw_ops->log_event) {
        char log_data[256];
        if (log_json_extra && log_json_extra[0]) {
            snprintf(log_data, sizeof(log_data),
                     "{\"overheatCount\":%u,\"type\":\"%s\",\"data\":%s}",
                     overheat_count, overheat_type_string(overheat_type),
                     log_json_extra);
        } else {
            snprintf(log_data, sizeof(log_data),
                     "{\"overheatCount\":%u,\"type\":\"%s\"}",
                     overheat_count, overheat_type_string(overheat_type));
        }

        if (severity == PM_SEVERITY_HARD) {
            hw_ops->log_event("power", "critical",
                "Overheat Mode Activated 3+ times, Restart Device Manually",
                log_data);
        } else {
            hw_ops->log_event("power", "critical",
                "Overheat mode activated - temperature exceeded threshold",
                log_data);
        }
    }

    /* Step 6: Handle recovery based on severity */
    if (severity == PM_SEVERITY_HARD) {
        /* Hard recovery - task terminates, manual restart required */
        if (hw_ops->task_delete_self) {
            hw_ops->task_delete_self();
        }
        /* Note: code below this won't execute if task_delete_self works */
    } else {
        /* Soft recovery - wait for cooling, then restart */
        if (hw_ops->delay_ms) {
            /* Wait in 1-second intervals to allow other tasks to run */
            uint32_t waited = 0;
            while (waited < SOFT_RECOVERY_COOLDOWN_MS) {
                hw_ops->delay_ms(RECOVERY_CHECK_INTERVAL_MS);
                waited += RECOVERY_CHECK_INTERVAL_MS;
            }
        }

        /* Clear overheat mode and restart */
        if (hw_ops->nvs_set_u16) {
            hw_ops->nvs_set_u16(NVS_KEY_OVERHEAT_MODE, 0);
        }

        if (hw_ops->log_event) {
            hw_ops->log_event("power", "info",
                "Overheat recovery completed - restarting system", "{}");
        }

        if (hw_ops->system_restart) {
            hw_ops->system_restart();
        }
    }
}

/* ============================================
 * Default Hardware Operations (Production)
 * ============================================ */

/* These are declared weak so they can be overridden in tests */
/* The actual implementations will be provided when this module
 * is integrated with power_management_task.c */

#ifdef ESP_PLATFORM
/* Production implementation - calls real hardware functions */

#include "EMC2101.h"
#include "vcore.h"
#include "nvs_config.h"
#include "dataBase.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Forward declaration for GPIO - defined in power_management_task.c */
extern void gpio_set_level(int gpio, int level);

static void default_set_fan_speed(float speed)
{
    EMC2101_set_fan_speed(speed);
}

static void default_set_vcore(float voltage_v, void* ctx)
{
    /* ctx is GlobalState* */
    VCORE_set_voltage((double)voltage_v, ctx);
}

static void default_set_asic_enable(int level)
{
    gpio_set_level(CONFIG_GPIO_ASIC_ENABLE, level);
}

static uint16_t default_nvs_get_u16(const char* key, uint16_t default_val)
{
    /* Map key names to NVS config keys */
    if (strcmp(key, NVS_KEY_OVERHEAT_COUNT) == 0) {
        return nvs_config_get_u16(NVS_CONFIG_OVERHEAT_COUNT, default_val);
    } else if (strcmp(key, NVS_KEY_ASIC_VOLTAGE) == 0) {
        return nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, default_val);
    } else if (strcmp(key, NVS_KEY_ASIC_FREQ) == 0) {
        return nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, default_val);
    } else if (strcmp(key, NVS_KEY_FAN_SPEED) == 0) {
        return nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, default_val);
    } else if (strcmp(key, NVS_KEY_AUTO_FAN_SPEED) == 0) {
        return nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, default_val);
    } else if (strcmp(key, NVS_KEY_OVERHEAT_MODE) == 0) {
        return nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, default_val);
    }
    return default_val;
}

static void default_nvs_set_u16(const char* key, uint16_t value)
{
    if (strcmp(key, NVS_KEY_OVERHEAT_COUNT) == 0) {
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_COUNT, value);
    } else if (strcmp(key, NVS_KEY_ASIC_VOLTAGE) == 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, value);
    } else if (strcmp(key, NVS_KEY_ASIC_FREQ) == 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, value);
    } else if (strcmp(key, NVS_KEY_FAN_SPEED) == 0) {
        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, value);
    } else if (strcmp(key, NVS_KEY_AUTO_FAN_SPEED) == 0) {
        nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, value);
    } else if (strcmp(key, NVS_KEY_OVERHEAT_MODE) == 0) {
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, value);
    }
}

static void default_log_event(const char* category, const char* level,
                               const char* message, const char* json_data)
{
    dataBase_log_event(category, level, message, json_data);
}

static void default_system_restart(void)
{
    esp_restart();
}

static void default_task_delete_self(void)
{
    vTaskDelete(NULL);
}

static void default_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static const overheat_hw_ops_t s_default_hw_ops = {
    .set_fan_speed = default_set_fan_speed,
    .set_vcore = default_set_vcore,
    .set_asic_enable = default_set_asic_enable,
    .nvs_get_u16 = default_nvs_get_u16,
    .nvs_set_u16 = default_nvs_set_u16,
    .log_event = default_log_event,
    .system_restart = default_system_restart,
    .task_delete_self = default_task_delete_self,
    .delay_ms = default_delay_ms
};

const overheat_hw_ops_t* overheat_get_default_hw_ops(void)
{
    return &s_default_hw_ops;
}

#else
/* Host testing stub - returns NULL, tests must provide their own ops */

const overheat_hw_ops_t* overheat_get_default_hw_ops(void)
{
    return NULL;
}

#endif /* ESP_PLATFORM */
