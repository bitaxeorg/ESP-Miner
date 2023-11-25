#include "DS4432U.h"
#include "EMC2101.h"
#include "INA260.h"
#include "bm1397.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "serial.h"
#include <string.h>

#define POLL_RATE 5000
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

static const char * TAG = "power_management";

static float _fbound(float value, float lower_bound, float upper_bound)
{
    if (value < lower_bound)
        return lower_bound;
    if (value > upper_bound)
        return upper_bound;

    return value;
}

void POWER_MANAGEMENT_task(void * pvParameters)
{

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    power_management->frequency_multiplier = 1;

    int last_frequency_increase = 0;

    bool read_power = INA260_installed();

    uint16_t frequency_target = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    uint16_t auto_fan_speed = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1);

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    while (1) {

        if (read_power == true) {
            power_management->voltage = INA260_read_voltage();
            power_management->power = INA260_read_power() / 1000;
            power_management->current = INA260_read_current();
        }
        power_management->fan_speed = EMC2101_get_fan_speed();

        if (strcmp(GLOBAL_STATE->asic_model, "BM1397") == 0) {

            power_management->chip_temp = EMC2101_get_external_temp();

            // Voltage
            // We'll throttle between 4.9v and 3.5v
            float voltage_multiplier =
                _fbound((power_management->voltage - VOLTAGE_MIN_THROTTLE) * (1 / (float) VOLTAGE_RANGE), 0, 1);

            // Temperature
            float temperature_multiplier = 1;
            float over_temp = -(THROTTLE_TEMP - power_management->chip_temp);
            if (over_temp > 0) {
                temperature_multiplier = (THROTTLE_TEMP_RANGE - over_temp) / THROTTLE_TEMP_RANGE;
            }

            float lowest_multiplier = 1;
            float multipliers[2] = {voltage_multiplier, temperature_multiplier};

            for (int i = 0; i < 2; i++) {
                if (multipliers[i] < lowest_multiplier) {
                    lowest_multiplier = multipliers[i];
                }
            }

            power_management->frequency_multiplier = lowest_multiplier;

            float target_frequency = _fbound(power_management->frequency_multiplier * frequency_target, 0, frequency_target);

            if (target_frequency < 50) {
                // TODO: Turn the chip off
            }

            // chip is coming back from a low/no voltage event
            if (power_management->frequency_value < 50 && target_frequency > 50) {
                // TODO recover gracefully?
                esp_restart();
            }

            if (power_management->frequency_value > target_frequency) {
                power_management->frequency_value = target_frequency;
                last_frequency_increase = 0;
                BM1397_send_hash_frequency(power_management->frequency_value);
                ESP_LOGI(TAG, "target %f, Freq %f, Temp %f, Power %f", target_frequency, power_management->frequency_value,
                         power_management->chip_temp, power_management->power);
            } else {
                if (last_frequency_increase > 120 && power_management->frequency_value != frequency_target) {
                    float add = (target_frequency + power_management->frequency_value) / 2;
                    power_management->frequency_value += _fbound(add, 2, 20);
                    BM1397_send_hash_frequency(power_management->frequency_value);
                    ESP_LOGI(TAG, "target %f, Freq %f, Temp %f, Power %f", target_frequency, power_management->frequency_value,
                             power_management->chip_temp, power_management->power);
                    last_frequency_increase = 60;
                } else {
                    last_frequency_increase++;
                }
            }
        } else if (strcmp(GLOBAL_STATE->asic_model, "BM1366") == 0) {
            power_management->chip_temp = EMC2101_get_internal_temp() + 5;

            if (power_management->chip_temp > THROTTLE_TEMP &&
                (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
                ESP_LOGE(TAG, "OVERHEAT");
                nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 990);
                nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
                nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
                nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
                exit(EXIT_FAILURE);
            }
        }

        if (auto_fan_speed == 1) {
            automatic_fan_speed(power_management->chip_temp);
        } else {
            EMC2101_set_fan_speed((float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100) / 100);
        }
        // ESP_LOGI(TAG, "target %f, Freq %f, Volt %f, Power %f", target_frequency, power_management->frequency_value,
        // power_management->voltage, power_management->power);
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}

// Set the fan speed between 20% min and 100% max based on chip temperature as input.
// The fan speed increases from 20% to 100% proportionally to the temperature increase from 50 and THROTTLE_TEMP
static void automatic_fan_speed(float chip_temp)
{
    double result = 0.0;
    double min_temp = 50.0;
    double min_fan_speed = 20.0;

    if (chip_temp < min_temp) {
        result = min_fan_speed;
    } else if (chip_temp >= THROTTLE_TEMP) {
        result = 100;
    } else {
        double temp_range = THROTTLE_TEMP - min_temp;
        double fan_range = 100 - min_fan_speed;
        result = ((chip_temp - min_temp) / temp_range) * fan_range + min_fan_speed;
    }

    EMC2101_set_fan_speed((float) result / 100);
}
