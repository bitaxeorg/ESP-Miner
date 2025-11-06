#include <string.h>
#include "INA260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "serial.h"
#include "TPS546.h"
#include "vcore.h"
#include "thermal.h"
#include "PID.h"
#include "power.h"
#include "asic.h"
#include "bm1370.h"
#include "utils.h"
#include "asic_init.h"
#include "asic_reset.h"
#include "driver/uart.h"

#define EPSILON 0.0001f
#define POLL_RATE 1800
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define SAFE_TEMP 45.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

#define ASIC_REDUCTION 100.0

static const char * TAG = "power_management";

double pid_input = 0.0;
double pid_output = 0.0;
double min_fan_pct;
double pid_setPoint;
double pid_p = 15.0;        
double pid_i = 0.2;
double pid_d = 3.0;
double pid_d_startup = 20.0;  // Higher D value for startup

bool pid_startup_phase = true;
int pid_startup_counter = 0;

// Hold and Ramp startup D-term
#define PID_STARTUP_HOLD_DURATION 3  // Number of cycles to HOLD pid_d_startup
#define PID_STARTUP_RAMP_DURATION 17 // Number of cycles to RAMP DOWN D (Total startup duration PID_STARTUP_HOLD_DURATION + PID_STARTUP_RAMP_DURATION)

PIDController pid;

static float expected_hashrate(GlobalState * GLOBAL_STATE, float frequency)
{
    return frequency * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0;
}

void POWER_MANAGEMENT_init_frequency(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = frequency;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = expected_hashrate(GLOBAL_STATE, frequency);
    
    char expected_hashrate_str[16] = {0};
    suffixString(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate * 1e6, expected_hashrate_str, sizeof(expected_hashrate_str), 0);
    ESP_LOGI(TAG, "ASIC Frequency: %g MHz, Expected hashrate: %sH/s", frequency, expected_hashrate_str);
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);
    
    float last_asic_frequency = power_management->frequency_value;

    pid_setPoint = (double)nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);
    min_fan_pct = (double)nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED);

    // Initialize PID controller with pid_d_startup and PID_REVERSE directly
    pid_init(&pid, &pid_input, &pid_output, &pid_setPoint, pid_p, pid_i, pid_d_startup, PID_P_ON_E, PID_REVERSE);
    pid_set_sample_time(&pid, POLL_RATE - 1); // Sample time in ms
    pid_set_output_limits(&pid, min_fan_pct, 100);
    pid_set_mode(&pid, AUTOMATIC);        // This calls pid_initialize() internally

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;

    uint16_t last_known_asic_voltage = 0;
    float last_known_asic_frequency = 0.0;

    while (1) {

        // Refresh PID setpoint from NVS in case it was changed via API
        pid_setPoint = (double)nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);

        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        power_management->power = Power_get_power(GLOBAL_STATE);

        power_management->fan_rpm = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
        power_management->fan2_rpm = Thermal_get_fan2_speed(&GLOBAL_STATE->DEVICE_CONFIG);
        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
        power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        bool asic_overheat = 
            power_management->chip_temp_avg > THROTTLE_TEMP
            || power_management->chip_temp2_avg > THROTTLE_TEMP;
        
        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || asic_overheat) && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            if (power_management->chip_temp2_avg > 0) {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC1: %fC ASIC2: %fC", power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
            } else {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC: %fC", power_management->vr_temp, power_management->chip_temp_avg);
            }
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1);

            VCORE_set_voltage(GLOBAL_STATE, 0.0f);
            
            ESP_LOGI(TAG, "Setting RST pin to low due to overheat condition");
            ESP_ERROR_CHECK(asic_hold_reset_low());

            last_known_asic_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
            last_known_asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
            nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, false);
            nvs_config_set_u16(NVS_CONFIG_MANUAL_FAN_SPEED, 100);
            nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, true);
            ESP_LOGW(TAG, "Entering safe mode due to overheat condition. System operation halted.");
            
            // Note: ASIC temperature readings are invalid when ASIC is powered down (returns -1)
            // For 600-series boards that use ASIC thermal diode, we rely on VR temp and fixed cooling time
            // For boards with EMC internal temp sensor, readings remain valid
            bool asic_temp_valid = GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp;
            int cooling_cycles = 0;
            const int MIN_COOLING_CYCLES = 6; // Minimum 30 seconds cooling
            
            while (cooling_cycles < MIN_COOLING_CYCLES || power_management->vr_temp > TPS546_THROTTLE_TEMP - 10) {
                vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait 5 seconds
                cooling_cycles++;
                
                power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
                
                // Only check ASIC temps if they're valid (not using ASIC thermal diode)
                if (asic_temp_valid) {
                    power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
                    power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);
                    ESP_LOGW(TAG, "Safe mode active (cycle %d) - VR: %.1fC ASIC1: %.1fC ASIC2: %.1fC",
                             cooling_cycles, power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
                    
                    // Continue if ASIC temps still too high
                    if (power_management->chip_temp_avg >  SAFE_TEMP || power_management->chip_temp2_avg > SAFE_TEMP) {
                        cooling_cycles = 0; // Reset cycle count if still hot
                    }
                } else {
                    // For boards using ASIC thermal diode (600 series), rely on VR temp and time
                    ESP_LOGW(TAG, "Safe mode active (cycle %d/%d) - VR: %.1fC (ASIC temps unavailable while powered down)",
                             cooling_cycles, MIN_COOLING_CYCLES, power_management->vr_temp);
                }
            }
            ESP_LOGI(TAG, "Temperature normalized after %d cooling cycles. Reinitializing ASIC...", cooling_cycles);
            
            uint16_t reduced_voltage = last_known_asic_voltage > ASIC_REDUCTION ? last_known_asic_voltage - ASIC_REDUCTION : 1000;
            float reduced_asic_frequency = last_known_asic_frequency > ASIC_REDUCTION ? last_known_asic_frequency - ASIC_REDUCTION : 400.0;
            
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, reduced_voltage);
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, reduced_asic_frequency);
            
            ESP_LOGI(TAG, "Restoring core voltage to %umV = %.3fV (reduced from %umV = %.3fV)...",
                     reduced_voltage, reduced_voltage/1000.0, last_known_asic_voltage, last_known_asic_voltage/1000.0);
            VCORE_set_voltage(GLOBAL_STATE, (double)reduced_voltage / 1000.0);
            vTaskDelay(500 / portTICK_PERIOD_MS); // Wait for voltage to stabilize
            
            ESP_LOGI(TAG, "Stopping ASIC tasks...");
            // Mark ASIC as uninitialized to stop any tasks from trying to use UART
            GLOBAL_STATE->ASIC_initalized = false;
            // Give tasks time to complete any current UART operation and notice the flag
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "Flushing UART buffers...");
            // flush driver to clear any stale data
            uart_flush(UART_NUM_1);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            
            // Perform live recovery
            // Stabilization delay of 2000ms prevents race conditions where tasks are just
            // starting to use ASIC while power management loop tries to change frequency
            uint8_t chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, 2000);
            
            if (chip_count > 0) {
                // Frequency reduction will now be applied by normal power management loop
                nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, false);
                ESP_LOGI(TAG, "Resuming normal operation. Reduced frequency (%.0f MHz) will be applied automatically.", reduced_asic_frequency);
            }
        }

        //enable the PID auto control for the FAN if set
        if (nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED)) {
            if (power_management->chip_temp_avg >= 0) { // Ignore invalid temperature readings (-1)
                if (power_management->chip_temp2_avg > power_management->chip_temp_avg) {
                    pid_input = power_management->chip_temp2_avg;
                } else {
                    pid_input = power_management->chip_temp_avg;
                }
                
                // Hold and Ramp logic for startup D value
                if (pid_startup_phase) {
                    pid_startup_counter++; // Increment counter at the start of each startup phase cycle
                    
                    if (pid_startup_counter >= (PID_STARTUP_HOLD_DURATION + PID_STARTUP_RAMP_DURATION)) {
                        // Transition complete, switch to normal D value
                        pid_set_tunings(&pid, pid_p, pid_i, pid_d); // Use normal pid_d
                        pid_startup_phase = false;
                        ESP_LOGI(TAG, "PID startup phase complete, switching to normal D value: %.1f", pid_d);
                    } else if (pid_startup_counter > PID_STARTUP_HOLD_DURATION) {
                        // In RAMP DOWN phase
                        int ramp_counter = pid_startup_counter - PID_STARTUP_HOLD_DURATION;
                        double current_d = pid_d_startup - ((pid_d_startup - pid_d) * (double)ramp_counter / PID_STARTUP_RAMP_DURATION);
                        pid_set_tunings(&pid, pid_p, pid_i, current_d);
                        ESP_LOGI(TAG, "PID startup ramp phase: %d/%d (Total cycle: %d), current D: %.1f", 
                                 ramp_counter, PID_STARTUP_RAMP_DURATION, pid_startup_counter, current_d);
                    } else {
                        // In HOLD phase, ensure pid_d_startup is used.
                        // pid_init already set it with pid_d_startup. If pid_p or pid_i changed dynamically,
                        // this call ensures pid_d_startup is maintained.
                        pid_set_tunings(&pid, pid_p, pid_i, pid_d_startup);
                        ESP_LOGI(TAG, "PID startup hold phase: %d/%d, holding D at: %.1f", 
                                 pid_startup_counter, PID_STARTUP_HOLD_DURATION, pid_d_startup);
                    }
                }
                // If not in startup_phase, PID tunings remain as set (either normal or last startup value if just exited)
                
                pid_compute(&pid);
                // Uncomment for debugging PID output directly after compute
                // ESP_LOGD(TAG, "DEBUG: PID raw output: %.2f%%, Input: %.1f, SetPoint: %.1f", pid_output, pid_input, pid_setPoint);

                power_management->fan_perc = pid_output;
                if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, pid_output / 100.0) != ESP_OK) {
                    exit(EXIT_FAILURE);
                }
                ESP_LOGI(TAG, "Temp: %.1f °C, SetPoint: %.1f °C, Output: %.1f%% (P:%.1f I:%.1f D_val:%.1f D_start_val:%.1f)",
                         pid_input, pid_setPoint, pid_output, pid.dispKp, pid.dispKi, pid.dispKd, pid_d_startup); // Log current effective Kp, Ki, Kd
            } else {
                if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
                    ESP_LOGW(TAG, "AP mode with invalid temperature reading: %.1f °C - Setting fan to 70%%", power_management->chip_temp_avg);
                    power_management->fan_perc = 70;
                    if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 0.7) != ESP_OK) {
                        exit(EXIT_FAILURE);
                    }
                } else {
                    ESP_LOGW(TAG, "Ignoring invalid temperature reading: %.1f °C", power_management->chip_temp_avg);
                    if (power_management->fan_perc < 100) {
                        ESP_LOGW(TAG, "Setting fan speed to 100%%");
                        power_management->fan_perc = 100;
                        if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1)) {
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            }
        } else { // Manual fan speed
            uint16_t fan_perc = nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED);
            if (fabs(power_management->fan_perc - fan_perc) > EPSILON) {
                ESP_LOGI(TAG, "Setting manual fan speed to %d%%", fan_perc);
                power_management->fan_perc = fan_perc;
                if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, fan_perc / 100.0f) != ESP_OK) {
                    exit(EXIT_FAILURE);
                }
            }
        }

        uint16_t core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
        float asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage(GLOBAL_STATE, (double) core_voltage / 1000.0);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", asic_frequency, last_asic_frequency);
            
            bool success = ASIC_set_frequency(GLOBAL_STATE, asic_frequency);
            
            if (success) {
                power_management->frequency_value = asic_frequency;
                power_management->expected_hashrate = expected_hashrate(GLOBAL_STATE, asic_frequency);
            }
            
            last_asic_frequency = asic_frequency;
        }

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
