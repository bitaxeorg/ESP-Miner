/**
 * @file overheat.h
 * @brief Overheat detection and recovery handling
 *
 * This module consolidates the duplicated overheat handling logic from
 * power_management_task.c into a single, testable interface.
 *
 * The module separates:
 * - Pure detection logic (uses power_management_calc.h functions)
 * - Recovery action interface (can be mocked for testing)
 */

#ifndef OVERHEAT_H
#define OVERHEAT_H

#include <stdint.h>
#include <stdbool.h>
#include "power_management_calc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Input data for overheat check
 *
 * Captures all necessary readings for overheat detection.
 */
typedef struct {
    float chip_temp;        /**< ASIC chip temperature in Celsius */
    float vr_temp;          /**< Voltage regulator temperature (0 if N/A) */
    uint16_t frequency;     /**< Current frequency in MHz */
    uint16_t voltage;       /**< Current core voltage in mV */
} overheat_check_input_t;

/**
 * @brief Result of overheat check with recommended action
 */
typedef struct {
    bool should_trigger;              /**< True if overheat protection should activate */
    pm_overheat_type_t overheat_type; /**< What's overheating */
    pm_overheat_severity_t severity;  /**< Soft vs hard recovery */
} overheat_check_result_t;

/**
 * @brief Device-specific configuration for recovery actions
 */
typedef struct {
    int device_model;       /**< DeviceModel enum value */
    int board_version;      /**< Board version number */
    bool has_power_en;      /**< Has GPIO power enable */
    bool has_tps546;        /**< Has TPS546 voltage regulator */
} overheat_device_config_t;

/**
 * @brief Safe values to apply during overheat recovery
 */
typedef struct {
    uint16_t voltage_mv;    /**< Safe voltage in mV */
    uint16_t frequency_mhz; /**< Safe frequency in MHz */
    uint16_t fan_speed_pct; /**< Fan speed percentage */
    bool auto_fan_off;      /**< Disable auto fan */
} overheat_safe_values_t;

/** Default safe values for overheat recovery */
#define OVERHEAT_SAFE_VALUES_DEFAULT { \
    .voltage_mv = 1000, \
    .frequency_mhz = 50, \
    .fan_speed_pct = 100, \
    .auto_fan_off = true \
}

/**
 * @brief Interface for hardware control during overheat recovery
 *
 * This interface allows mocking hardware operations for testing.
 */
typedef struct overheat_hw_ops {
    /** Set fan speed (0.0 to 1.0) */
    void (*set_fan_speed)(float speed);

    /** Set core voltage (0.0 to disable) */
    void (*set_vcore)(float voltage_v, void* ctx);

    /** Set GPIO level for ASIC enable */
    void (*set_asic_enable)(int level);

    /** Get/set NVS u16 values */
    uint16_t (*nvs_get_u16)(const char* key, uint16_t default_val);
    void (*nvs_set_u16)(const char* key, uint16_t value);

    /** Log event to database */
    void (*log_event)(const char* category, const char* level,
                      const char* message, const char* json_data);

    /** Restart the system */
    void (*system_restart)(void);

    /** Delete current task (for hard recovery) */
    void (*task_delete_self)(void);

    /** Delay in milliseconds */
    void (*delay_ms)(uint32_t ms);
} overheat_hw_ops_t;

/**
 * @brief Check for overheat condition (pure function wrapper)
 *
 * Uses the pure functions from power_management_calc.h to determine
 * if overheat protection should trigger and what severity.
 *
 * @param input Current temperature and voltage/frequency readings
 * @param overheat_count Current overheat event count (before increment)
 * @return Result with trigger decision, type, and severity
 */
overheat_check_result_t overheat_check(const overheat_check_input_t* input,
                                        uint16_t overheat_count);

/**
 * @brief Execute overheat recovery actions
 *
 * Performs all hardware actions needed for overheat recovery:
 * - Sets fan to maximum
 * - Disables ASIC power (device-specific)
 * - Sets safe NVS values
 * - Logs the event
 * - For soft recovery: waits, applies preset, restarts
 * - For hard recovery: deletes task (no restart)
 *
 * @param severity Soft or hard recovery
 * @param device_config Device-specific configuration
 * @param safe_values Safe values to apply (NULL for defaults)
 * @param hw_ops Hardware operations interface
 * @param hw_ctx Context passed to hardware operations
 * @param overheat_type Type of overheat for logging
 * @param log_json_extra Additional JSON data for logging (can be NULL)
 */
void overheat_execute_recovery(pm_overheat_severity_t severity,
                                const overheat_device_config_t* device_config,
                                const overheat_safe_values_t* safe_values,
                                const overheat_hw_ops_t* hw_ops,
                                void* hw_ctx,
                                pm_overheat_type_t overheat_type,
                                const char* log_json_extra);

/**
 * @brief Format overheat event data as JSON
 *
 * Creates JSON string with chip temp, VR temp, thresholds, and device info.
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param input Overheat check input data
 * @param device_name Device name string (e.g., "DEVICE_MAX")
 * @return Number of characters written (excluding null terminator)
 */
int overheat_format_log_data(char* buf, size_t buf_size,
                              const overheat_check_input_t* input,
                              const char* device_name);

/**
 * @brief Format device info string for logging
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param input Overheat check input data
 * @param device_name Device name string
 * @return Number of characters written
 */
int overheat_format_device_info(char* buf, size_t buf_size,
                                 const overheat_check_input_t* input,
                                 const char* device_name);

/**
 * @brief Get default hardware operations for production use
 *
 * Returns an operations struct that calls the real hardware functions.
 * This should only be used in production code, not tests.
 *
 * @return Pointer to static operations struct
 */
const overheat_hw_ops_t* overheat_get_default_hw_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* OVERHEAT_H */
