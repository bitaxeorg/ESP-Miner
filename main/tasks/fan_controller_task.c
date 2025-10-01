#include <string.h>
#include "INA260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "fan_controller_task.h"
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

#define POLL_RATE 100

static const char * TAG = "fan_controller";

void FAN_CONTROLLER_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    PIDController pid = {0};

    double pid_input = 0.0;
    double pid_output = 0.0;
    double min_fan_pct = 25.0;
    double pid_setPoint = 60.0; // Default, will be overwritten by NVS
    double pid_p = 2.0;
    double pid_i = 0.1;
    double pid_d = 1.0;
    float filtered_temp = 60.0;
    float alpha = 0.2;
    int log_counter = 0;

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    pid_setPoint = (double)nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, pid_setPoint);
    min_fan_pct = (double)nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED, min_fan_pct);

    // Initialize PID controller with pid_d_startup and PID_REVERSE directly
    pid_init(&pid, &pid_input, &pid_output, &pid_setPoint, pid_p, pid_i, pid_d, PID_P_ON_E, PID_REVERSE);
    pid_set_sample_time(&pid, POLL_RATE - 1); // Sample time in ms
    pid_set_output_limits(&pid, min_fan_pct, 100);
    pid_set_mode(&pid, AUTOMATIC);        // This calls pid_initialize() internally

    while (1) {
        // Refresh PID setpoint from NVS in case it was changed via API
        pid_setPoint = (double)nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, pid_setPoint);

        //enable the PID auto control for the FAN if set
        if (nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {
            if (power_management->chip_temp_avg >= 0) { // Ignore invalid temperature readings (-1)
                if (power_management->chip_temp2_avg > 0) {
                    pid_input = (power_management->chip_temp_avg + power_management->chip_temp2_avg) / 2.0; // TODO: Or max of both?
                } else {
                    pid_input = power_management->chip_temp_avg;
                }

                float raw_temp = power_management->chip_temp_avg;
                filtered_temp = alpha * raw_temp + (1.0 - alpha) * filtered_temp;
                pid_input = filtered_temp;
                
                pid_compute(&pid);
                // Uncomment for debugging PID output directly after compute
                // ESP_LOGD(TAG, "DEBUG: PID raw output: %.2f%%, Input: %.1f, SetPoint: %.1f", pid_output, pid_input, pid_setPoint);

                if ((uint16_t) pid_output != power_management->fan_perc) {
                    power_management->fan_perc = (uint16_t) pid_output;
                    Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, pid_output / 100.0);
                }

                log_counter += POLL_RATE;
                if (log_counter >= 2000) {
                    log_counter = 0;
                    ESP_LOGI(TAG, "Temp: %.1f °C, SetPoint: %.1f °C, Output: %.1f%% (P:%.1f I:%.1f D_val:%.1f)",
                            pid_input, pid_setPoint, pid_output, pid.dispKp, pid.dispKi, pid.dispKd); // Log current effective Kp, Ki, Kd
                }
                    
            } else {
                if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
                    ESP_LOGW(TAG, "AP mode with invalid temperature reading: %.1f °C - Setting fan to 70%%", power_management->chip_temp_avg);
                    power_management->fan_perc = 70;
                    Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 0.7);
                } else {
                    ESP_LOGW(TAG, "Ignoring invalid temperature reading: %.1f °C", power_management->chip_temp_avg);
                }
            }
        } else { // Manual fan speed
            float fs = (float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
            power_management->fan_perc = fs;
            Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, (float) fs / 100.0);
        }

        power_management->fan_rpm = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
