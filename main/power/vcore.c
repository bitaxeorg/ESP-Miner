#include "esp_log.h"
#include <math.h>
#include <stdio.h>

#include "DS4432U.h"
#include "INA260.h"
#include "TPS546.h"
#include "adc.h"
#include "driver/gpio.h"
#include "vcore.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_PLUG_SENSE CONFIG_GPIO_PLUG_SENSE

static const char *TAG = "vcore";

static TPS546_CONFIG get_tps546_config(const FamilyConfig * family)
{
    TPS546_CONFIG config = {0};

    // Set family-specific parameters
    switch (family->id) {
    case GAMMA_TURBO:
        config.TPS546_INIT_PHASE = TPS546_INIT_PHASE_MULTI;
        config.TPS546_INIT_VIN_ON = 11.0;
        config.TPS546_INIT_VIN_OFF = 10.5;
        config.TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0;
        config.TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0;
        config.TPS546_INIT_SCALE_LOOP = 0.25;
        config.TPS546_INIT_VOUT_MIN = 1;
        config.TPS546_INIT_VOUT_MAX = 3;
        config.TPS546_INIT_VOUT_COMMAND = 1.2;
        config.TPS546_INIT_IOUT_OC_WARN_LIMIT = 50.00;
        config.TPS546_INIT_IOUT_OC_FAULT_LIMIT = 55.00;
        // Multi-phase stacking configuration for 2 TPS modules
        config.TPS546_INIT_STACK_CONFIG = 0x0001; // 2 modules (One-Slave, 2-phase)
        config.TPS546_INIT_SYNC_CONFIG = 0xD0;    // Enable Auto Detect SYNC
        config.TPS546_INIT_COMPENSATION_CONFIG[0] = 0x12;
        config.TPS546_INIT_COMPENSATION_CONFIG[1] = 0x34;
        config.TPS546_INIT_COMPENSATION_CONFIG[2] = 0x42;
        config.TPS546_INIT_COMPENSATION_CONFIG[3] = 0x21;
        config.TPS546_INIT_COMPENSATION_CONFIG[4] = 0x04;
        break;

    case HEX:
    case SUPRA_HEX:
        config.TPS546_INIT_PHASE = TPS546_INIT_PHASE_SINGLE;
        config.TPS546_INIT_VIN_ON = 11.5;
        config.TPS546_INIT_VIN_OFF = 11.0;
        config.TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0;
        config.TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0;
        config.TPS546_INIT_SCALE_LOOP = 0.125;
        config.TPS546_INIT_VOUT_MIN = 2.5;
        config.TPS546_INIT_VOUT_MAX = 4.5;
        config.TPS546_INIT_VOUT_COMMAND = 3.6;
        config.TPS546_INIT_IOUT_OC_WARN_LIMIT = 25.00;
        config.TPS546_INIT_IOUT_OC_FAULT_LIMIT = 30.00;
        // Single-phase configuration
        config.TPS546_INIT_STACK_CONFIG = 0x0000; // 1 module
        config.TPS546_INIT_SYNC_CONFIG = 0x10;    // Disable SYNC
        break;

    default: // MAX, ULTRA, SUPRA, GAMMA
        config.TPS546_INIT_PHASE = TPS546_INIT_PHASE_SINGLE;
        config.TPS546_INIT_VIN_ON = 4.8;
        config.TPS546_INIT_VIN_OFF = 4.5;
        config.TPS546_INIT_VIN_UV_WARN_LIMIT = 0;
        config.TPS546_INIT_VIN_OV_FAULT_LIMIT = 6.5;
        config.TPS546_INIT_SCALE_LOOP = 0.25;
        config.TPS546_INIT_VOUT_MIN = 1;
        config.TPS546_INIT_VOUT_MAX = 2;
        config.TPS546_INIT_VOUT_COMMAND = 1.2;
        config.TPS546_INIT_IOUT_OC_WARN_LIMIT = 25.00;
        config.TPS546_INIT_IOUT_OC_FAULT_LIMIT = 30.00;
        // Single-phase configuration
        config.TPS546_INIT_STACK_CONFIG = 0x0000; // 1 module
        config.TPS546_INIT_SYNC_CONFIG = 0x10;    // Disable SYNC
        break;
    }

    return config;
}

esp_err_t VCORE_init(GlobalState * GLOBAL_STATE)
{
    ESP_RETURN_ON_FALSE(GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains != 0, ESP_FAIL, TAG, "voltage_domains not defined");

    if (GLOBAL_STATE->DEVICE_CONFIG.DS4432U) {
        ESP_RETURN_ON_ERROR(DS4432U_init(), TAG, "DS4432 init failed!");
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        ESP_RETURN_ON_ERROR(INA260_init(), TAG, "INA260 init failed!");
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        TPS546_CONFIG tps_config = get_tps546_config(&GLOBAL_STATE->DEVICE_CONFIG.family);
        ESP_RETURN_ON_ERROR(TPS546_init(tps_config), TAG, "TPS546 init failed!");
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.plug_sense) {
        gpio_config_t barrel_jack_conf = {
            .pin_bit_mask = (1ULL << GPIO_PLUG_SENSE),
            .mode = GPIO_MODE_INPUT,
        };
        gpio_config(&barrel_jack_conf);
        int barrel_jack_plugged_in = gpio_get_level(GPIO_PLUG_SENSE);

        gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT);
        if (barrel_jack_plugged_in == 1 || GLOBAL_STATE->DEVICE_CONFIG.asic_enable) {
            gpio_set_level(GPIO_ASIC_ENABLE, 0);
        } else {
            // turn ASIC off
            gpio_set_level(GPIO_ASIC_ENABLE, 1);
        }
    }

    return ESP_OK;
}

esp_err_t VCORE_set_voltage(GlobalState * GLOBAL_STATE, float core_voltage)
{
    ESP_LOGI(TAG, "Set ASIC voltage = %.3fV", core_voltage);

    if (GLOBAL_STATE->DEVICE_CONFIG.DS4432U) {
        if (core_voltage != 0.0f) {
            ESP_RETURN_ON_ERROR(DS4432U_set_voltage(core_voltage), TAG, "DS4432U set voltage failed!");
        }
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        uint16_t voltage_domains = GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains;
        ESP_RETURN_ON_ERROR(TPS546_set_vout(core_voltage * voltage_domains), TAG, "TPS546 set voltage failed!");
    }
    if (core_voltage == 0.0f && GLOBAL_STATE->DEVICE_CONFIG.asic_enable) {
        gpio_set_level(GPIO_ASIC_ENABLE, 1);
    }

    return ESP_OK;
}

int16_t VCORE_get_voltage_mv(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_vout() / GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains * 1000;
    }
    return ADC_get_vcore();
}

esp_err_t VCORE_check_fault(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        ESP_RETURN_ON_ERROR(TPS546_check_status(GLOBAL_STATE), TAG, "TPS546 check status failed!");
    }
    return ESP_OK;
}

const char * VCORE_get_fault_string(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_error_message();
    }
    return NULL;
}
