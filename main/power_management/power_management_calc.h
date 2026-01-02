/**
 * @file power_management_calc.h
 * @brief Pure calculation functions for power management
 *
 * This module contains hardware-independent calculation functions that can be
 * unit tested without ESP32 hardware. These functions are extracted from
 * power_management_task.c to enable testability.
 *
 * Design principles:
 * - No global state access
 * - No hardware I/O
 * - No side effects
 * - All inputs via parameters, all outputs via return values
 */

#ifndef POWER_MANAGEMENT_CALC_H
#define POWER_MANAGEMENT_CALC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * Constants
 * ============================================ */

/** Temperature at which throttling begins */
#define PM_THROTTLE_TEMP 75.0f

/** Maximum safe temperature before overheat mode */
#define PM_MAX_TEMP 90.0f

/** TPS546 voltage regulator throttle temperature */
#define PM_TPS546_THROTTLE_TEMP 105.0f

/** TPS546 voltage regulator maximum temperature */
#define PM_TPS546_MAX_TEMP 145.0f

/** Minimum temperature for fan control scaling */
#define PM_MIN_FAN_TEMP 45.0f

/** Minimum fan speed percentage */
#define PM_MIN_FAN_SPEED 35.0f

/** Autotune target temperature */
#define PM_AUTOTUNE_TARGET_TEMP 60

/** Warmup period in seconds before autotune activates */
#define PM_AUTOTUNE_WARMUP_SECONDS 900

/** Consecutive low hashrate attempts before preset reset */
#define PM_MAX_LOW_HASHRATE_ATTEMPTS 3

/** Hashrate threshold as percentage of target */
#define PM_HASHRATE_THRESHOLD_PERCENT 50.0f

/* ============================================
 * Fan Speed Calculation
 * ============================================ */

/**
 * @brief Calculate automatic fan speed based on chip temperature
 *
 * Implements a linear scaling of fan speed between min_temp and throttle_temp.
 * Below min_temp: returns minimum fan speed
 * Above throttle_temp: returns 100%
 * Between: linear interpolation
 *
 * @param chip_temp Current chip temperature in Celsius
 * @return Fan speed as percentage (0.0 to 100.0)
 */
float pm_calc_fan_speed_percent(float chip_temp);

/**
 * @brief Calculate fan speed with custom parameters
 *
 * Same as pm_calc_fan_speed_percent but allows custom thresholds for testing.
 *
 * @param chip_temp Current chip temperature
 * @param min_temp Temperature below which fan runs at minimum
 * @param throttle_temp Temperature at which fan runs at 100%
 * @param min_fan_speed Minimum fan speed percentage
 * @return Fan speed as percentage (0.0 to 100.0)
 */
float pm_calc_fan_speed_percent_ex(float chip_temp, float min_temp,
                                    float throttle_temp, float min_fan_speed);

/* ============================================
 * Overheat Detection
 * ============================================ */

/**
 * @brief Result of overheat check
 */
typedef enum {
    PM_OVERHEAT_NONE,      /**< No overheat condition */
    PM_OVERHEAT_CHIP,      /**< Chip temperature exceeds threshold */
    PM_OVERHEAT_VR,        /**< Voltage regulator temperature exceeds threshold */
    PM_OVERHEAT_BOTH       /**< Both chip and VR overheating */
} pm_overheat_type_t;

/**
 * @brief Severity of overheat condition
 */
typedef enum {
    PM_SEVERITY_NONE,      /**< No action needed */
    PM_SEVERITY_SOFT,      /**< Soft recovery (wait and restart) */
    PM_SEVERITY_HARD       /**< Hard recovery (requires manual intervention) */
} pm_overheat_severity_t;

/**
 * @brief Check if overheat condition exists
 *
 * @param chip_temp Current ASIC chip temperature
 * @param vr_temp Current voltage regulator temperature (0 if not available)
 * @param chip_threshold Chip temperature threshold
 * @param vr_threshold VR temperature threshold
 * @return Type of overheat condition
 */
pm_overheat_type_t pm_check_overheat(float chip_temp, float vr_temp,
                                      float chip_threshold, float vr_threshold);

/**
 * @brief Check if system should enter overheat protection
 *
 * Combines temperature check with frequency/voltage thresholds to determine
 * if overheat protection should trigger.
 *
 * @param chip_temp Current ASIC chip temperature
 * @param vr_temp Current voltage regulator temperature
 * @param frequency Current frequency in MHz
 * @param voltage Current voltage in mV
 * @return true if overheat protection should trigger
 */
bool pm_should_trigger_overheat(float chip_temp, float vr_temp,
                                 uint16_t frequency, uint16_t voltage);

/**
 * @brief Determine overheat severity based on event count
 *
 * Every 3rd overheat event triggers hard recovery.
 *
 * @param overheat_count Current overheat event count (before increment)
 * @return Severity level for recovery action
 */
pm_overheat_severity_t pm_calc_overheat_severity(uint16_t overheat_count);

/* ============================================
 * Autotune Calculations
 * ============================================ */

/**
 * @brief Input parameters for autotune calculation
 */
typedef struct {
    float chip_temp;           /**< Current ASIC temperature in Celsius */
    float current_hashrate;    /**< Current hashrate in GH/s */
    float target_hashrate;     /**< Expected hashrate based on frequency */
    uint16_t current_frequency; /**< Current frequency in MHz */
    uint16_t current_voltage;   /**< Current voltage in mV */
    int16_t current_power;      /**< Current power consumption in W */
    uint32_t uptime_seconds;    /**< System uptime in seconds */
} pm_autotune_input_t;

/**
 * @brief Autotune limits configuration
 */
typedef struct {
    uint16_t max_frequency;    /**< Maximum allowed frequency in MHz */
    uint16_t min_frequency;    /**< Minimum allowed frequency in MHz */
    uint16_t max_voltage;      /**< Maximum allowed voltage in mV */
    uint16_t min_voltage;      /**< Minimum allowed voltage in mV */
    int16_t max_power;         /**< Maximum power consumption in W */
} pm_autotune_limits_t;

/**
 * @brief Autotune decision output
 */
typedef struct {
    bool should_adjust;         /**< True if adjustments should be made */
    uint16_t new_frequency;     /**< New frequency (0 = no change) */
    uint16_t new_voltage;       /**< New voltage (0 = no change) */
    bool should_reset_preset;   /**< True if preset should be reapplied */
    bool skip_reason_warmup;    /**< Skipped due to warmup period */
    bool skip_reason_timing;    /**< Skipped due to timing interval */
    bool skip_reason_invalid;   /**< Skipped due to invalid inputs */
} pm_autotune_decision_t;

/**
 * @brief Calculate autotune adjustments
 *
 * Pure function that determines what frequency/voltage adjustments to make
 * based on current system state. Does not apply changes - caller is responsible
 * for that.
 *
 * @param input Current system state
 * @param limits Configured limits
 * @param target_temp Target temperature in Celsius
 * @param consecutive_low_hashrate Number of consecutive low hashrate events
 * @param ms_since_last_adjust Milliseconds since last adjustment
 * @return Decision struct with recommended actions
 */
pm_autotune_decision_t pm_calc_autotune(const pm_autotune_input_t* input,
                                         const pm_autotune_limits_t* limits,
                                         uint8_t target_temp,
                                         uint8_t consecutive_low_hashrate,
                                         uint32_t ms_since_last_adjust);

/**
 * @brief Check if hashrate is below threshold
 *
 * @param current_hashrate Current measured hashrate
 * @param target_hashrate Expected hashrate
 * @param threshold_percent Percentage threshold (e.g., 50.0 for 50%)
 * @return true if hashrate is below threshold
 */
bool pm_is_hashrate_low(float current_hashrate, float target_hashrate,
                        float threshold_percent);

/**
 * @brief Calculate target hashrate from frequency and core count
 *
 * @param frequency Current frequency in MHz
 * @param small_core_count Number of small cores per ASIC
 * @param asic_count Number of ASICs
 * @return Expected hashrate in GH/s
 */
float pm_calc_target_hashrate(uint16_t frequency, uint16_t small_core_count,
                               uint16_t asic_count);

/**
 * @brief Get autotune timing interval based on temperature
 *
 * Returns shorter intervals when temperature is elevated to react faster.
 *
 * @param chip_temp Current temperature
 * @return Interval in milliseconds
 */
uint32_t pm_get_autotune_interval_ms(float chip_temp);

/* ============================================
 * Utility Functions
 * ============================================ */

/**
 * @brief Calculate mining efficiency
 *
 * @param power Power consumption in Watts
 * @param hashrate Hashrate in GH/s
 * @return Efficiency in J/TH (Joules per Terahash)
 */
float pm_calc_efficiency(float power, float hashrate);

/**
 * @brief Clamp a value to a range
 *
 * @param value Value to clamp
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @return Clamped value
 */
float pm_clamp_f(float value, float min, float max);

/**
 * @brief Clamp a uint16 value to a range
 */
uint16_t pm_clamp_u16(uint16_t value, uint16_t min, uint16_t max);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGEMENT_CALC_H */
