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

#define EPSILON 0.0001f
#define POLL_TIME_MS 100
#define LOG_TIME_MS 2000

#define PID_P 3.0
#define PID_I 0.1
#define PID_D 1.0

static const char * TAG = "fan_controller";

void FAN_CONTROLLER_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    PIDController pid = {0};

    float pid_input = 0;
    float pid_output = 0;
    float pid_setPoint = nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);
    uint16_t pid_output_min = 0;
    int log_counter = 0;

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    // Initialize PID controller with pid_d_startup and PID_REVERSE directly
    pid_init(&pid, &pid_input, &pid_output, &pid_setPoint, PID_P, PID_I, PID_D, PID_P_ON_E, PID_REVERSE);
    pid_set_sample_time(&pid, POLL_TIME_MS); // Sample time in ms
    pid_set_mode(&pid, AUTOMATIC);        // This calls pid_initialize() internally

    ESP_LOGI(TAG, "P:%.1f I:%.1f D:%.1f", pid.dispKp, pid.dispKi, pid.dispKd);

    TickType_t taskWakeTime = xTaskGetTickCount();

    while (1) {
        if (nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE)) {
            if (fabs(power_management->fan_perc - 100) > EPSILON) {
                ESP_LOGW(TAG, "Overheat mode, setting fan to 100%%");
                power_management->fan_perc = 100;
                if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1.0f) != ESP_OK) {
                    exit(EXIT_FAILURE);
                }
            }
        } else {
            //enable the PID auto control for the FAN if set
            if (nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED)) {

                // Refresh PID setpoint from NVS in case it was changed via API
                pid_setPoint = nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);

                uint16_t new_pid_output_min = nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED);
                if (pid_output_min != new_pid_output_min) {
                    pid_output_min = new_pid_output_min;
                    pid_set_output_limits(&pid, pid_output_min, 100);
                }

                if (power_management->chip_temp_avg >= 0) { // Ignore invalid temperature readings (-1)
                    if (power_management->chip_temp2_avg > power_management->chip_temp_avg) {
                        pid_input = power_management->chip_temp2_avg;
                    } else {
                        pid_input = power_management->chip_temp_avg;
                    }
                    
                    pid_compute(&pid);
                    // Uncomment for debugging PID output directly after compute
                    // ESP_LOGD(TAG, "DEBUG: PID raw output: %.2f%%, Input: %.1f, SetPoint: %.1f", pid_output, pid_input, pid_setPoint);

                    if (fabs(power_management->fan_perc - pid_output) > EPSILON) {
                        power_management->fan_perc = pid_output;
                        if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, pid_output / 100.0f) != ESP_OK) {
                            exit(EXIT_FAILURE);
                        }
                    }

                    log_counter += POLL_TIME_MS;
                    if (log_counter >= LOG_TIME_MS) {
                        log_counter -= LOG_TIME_MS;
                        ESP_LOGI(TAG, "Temp: %.1f °C, SetPoint: %.1f °C, Output: %.1f%%", pid_input, pid_setPoint, pid_output);
                    }
                } else {
                    if (fabs(power_management->fan_perc - 70) > EPSILON) {
                        ESP_LOGI(TAG, "Temperature sensor starting up, setting fan to 70%%");
                        power_management->fan_perc = 70;
                        if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 0.7f) != ESP_OK) {
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            } else { // Manual fan speed
                uint16_t fan_perc = nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED);
                if (fabs(power_management->fan_perc - fan_perc) > EPSILON) {
                    power_management->fan_perc = fan_perc;
                    if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, fan_perc / 100.0f) != ESP_OK) {
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

        power_management->fan_rpm = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
        power_management->fan2_rpm = Thermal_get_fan2_speed(&GLOBAL_STATE->DEVICE_CONFIG);

        vTaskDelayUntil(&taskWakeTime, POLL_TIME_MS / portTICK_PERIOD_MS);
    }
}
