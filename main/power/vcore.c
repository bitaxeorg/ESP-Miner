#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "DS4432U.h"
#include "INA260.h"
#include "TPS546.h"
#include "nvs_config.h"
#include "adc.h"
#include "driver/gpio.h"
#include "global_state.h"
#include "device_config.h"
#include "vcore.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_PLUG_SENSE CONFIG_GPIO_PLUG_SENSE

static const char *TAG = "vcore";
static bool vcore_initialized = false;

static void log_tps546_config(const FamilyConfig *family, const TPS546_CONFIG *config)
{
    ESP_LOGI(TAG, "Selected TPS546 config for family=%s id=%d voltage_domains=%u",
             family->name, (int)family->id, (unsigned)family->voltage_domains);
    ESP_LOGI(TAG, "TPS546 init core: phase=0x%02X stack=0x%04X sync=0x%02X scale=%.3f vout=%.2f min=%.2f max=%.2f",
             config->TPS546_INIT_PHASE,
             config->TPS546_INIT_STACK_CONFIG,
             config->TPS546_INIT_SYNC_CONFIG,
             config->TPS546_INIT_SCALE_LOOP,
             config->TPS546_INIT_VOUT_COMMAND,
             config->TPS546_INIT_VOUT_MIN,
             config->TPS546_INIT_VOUT_MAX);
    ESP_LOGI(TAG, "TPS546 init VIN: on=%.2f off=%.2f uv_warn=%.2f ov_fault=%.2f",
             config->TPS546_INIT_VIN_ON,
             config->TPS546_INIT_VIN_OFF,
             config->TPS546_INIT_VIN_UV_WARN_LIMIT,
             config->TPS546_INIT_VIN_OV_FAULT_LIMIT);
    ESP_LOGI(TAG, "TPS546 init IOUT: warn=%.2f fault=%.2f",
             config->TPS546_INIT_IOUT_OC_WARN_LIMIT,
             config->TPS546_INIT_IOUT_OC_FAULT_LIMIT);
    ESP_LOGI(TAG, "TPS546 init COMPENSATION_CONFIG: %02X %02X %02X %02X %02X",
             config->TPS546_INIT_COMPENSATION_CONFIG[0],
             config->TPS546_INIT_COMPENSATION_CONFIG[1],
             config->TPS546_INIT_COMPENSATION_CONFIG[2],
             config->TPS546_INIT_COMPENSATION_CONFIG[3],
             config->TPS546_INIT_COMPENSATION_CONFIG[4]);
}

static TPS546_CONFIG get_tps546_config(const FamilyConfig * family)
{
    TPS546_CONFIG config = family->tps546_config ? *family->tps546_config : TPS546_CONFIG_DEFAULT;

    if (nvs_config_has_key(NVS_CONFIG_TPS546_PHASE)) {
        config.TPS546_INIT_PHASE = (uint8_t)nvs_config_get_u16(NVS_CONFIG_TPS546_PHASE);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VIN_ON)) {
        config.TPS546_INIT_VIN_ON = nvs_config_get_float(NVS_CONFIG_TPS546_VIN_ON);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VIN_OFF)) {
        config.TPS546_INIT_VIN_OFF = nvs_config_get_float(NVS_CONFIG_TPS546_VIN_OFF);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VIN_UV_WARN)) {
        config.TPS546_INIT_VIN_UV_WARN_LIMIT = nvs_config_get_float(NVS_CONFIG_TPS546_VIN_UV_WARN);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VIN_OV_FAULT)) {
        config.TPS546_INIT_VIN_OV_FAULT_LIMIT = nvs_config_get_float(NVS_CONFIG_TPS546_VIN_OV_FAULT);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_SCALE_LOOP)) {
        config.TPS546_INIT_SCALE_LOOP = nvs_config_get_float(NVS_CONFIG_TPS546_SCALE_LOOP);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VOUT_MIN)) {
        config.TPS546_INIT_VOUT_MIN = nvs_config_get_float(NVS_CONFIG_TPS546_VOUT_MIN);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VOUT_MAX)) {
        config.TPS546_INIT_VOUT_MAX = nvs_config_get_float(NVS_CONFIG_TPS546_VOUT_MAX);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_VOUT_COMMAND)) {
        config.TPS546_INIT_VOUT_COMMAND = nvs_config_get_float(NVS_CONFIG_TPS546_VOUT_COMMAND);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_IOUT_OC_WARN)) {
        config.TPS546_INIT_IOUT_OC_WARN_LIMIT = nvs_config_get_float(NVS_CONFIG_TPS546_IOUT_OC_WARN);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_IOUT_OC_FAULT)) {
        config.TPS546_INIT_IOUT_OC_FAULT_LIMIT = nvs_config_get_float(NVS_CONFIG_TPS546_IOUT_OC_FAULT);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_STACK_CONFIG)) {
        config.TPS546_INIT_STACK_CONFIG = nvs_config_get_u16(NVS_CONFIG_TPS546_STACK_CONFIG);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_SYNC_CONFIG)) {
        config.TPS546_INIT_SYNC_CONFIG = (uint8_t)nvs_config_get_u16(NVS_CONFIG_TPS546_SYNC_CONFIG);
    }
    if (nvs_config_has_key(NVS_CONFIG_TPS546_FREQUENCY)) {
        config.TPS546_INIT_FREQUENCY = nvs_config_get_u16(NVS_CONFIG_TPS546_FREQUENCY);
    }

    log_tps546_config(family, &config);
    return config;
}

static void configure_asic_power_enable(GlobalState * GLOBAL_STATE)
{
    if (!(GLOBAL_STATE->DEVICE_CONFIG.plug_sense || GLOBAL_STATE->DEVICE_CONFIG.asic_enable)) {
        return;
    }

    bool enable_power = GLOBAL_STATE->DEVICE_CONFIG.asic_enable;

    if (GLOBAL_STATE->DEVICE_CONFIG.plug_sense) {
        gpio_config_t barrel_jack_conf = {
            .pin_bit_mask = (1ULL << GPIO_PLUG_SENSE),
            .mode = GPIO_MODE_INPUT,
        };
        gpio_config(&barrel_jack_conf);
        enable_power = gpio_get_level(GPIO_PLUG_SENSE) == 1 || enable_power;
    }

    gpio_config_t asic_enable_conf = {
        .pin_bit_mask = (1ULL << GPIO_ASIC_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&asic_enable_conf);

    bool active_high = GLOBAL_STATE->DEVICE_CONFIG.asic_enable_active_high;
    gpio_set_level(GPIO_ASIC_ENABLE, enable_power ? active_high : !active_high);

    if (enable_power) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t VCORE_init(GlobalState * GLOBAL_STATE)
{
    vcore_initialized = false;
    ESP_RETURN_ON_FALSE(GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains != 0, ESP_FAIL, TAG, "voltage_domains not defined");

    configure_asic_power_enable(GLOBAL_STATE);

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

    vcore_initialized = true;
    return ESP_OK;
}

bool VCORE_is_initialized(void)
{
    return vcore_initialized;
}

esp_err_t VCORE_set_voltage(GlobalState * GLOBAL_STATE, float core_voltage)
{
    ESP_LOGI(TAG, "Set ASIC voltage = %.3fV", core_voltage);

    // Enable/disable the ASIC power enable GPIO before touching the regulator
    if (GLOBAL_STATE->DEVICE_CONFIG.asic_enable) {
        bool active_high = GLOBAL_STATE->DEVICE_CONFIG.asic_enable_active_high;
        bool enable_power = core_voltage != 0.0f;
        gpio_set_level(GPIO_ASIC_ENABLE, enable_power ? active_high : !active_high);

        if (enable_power) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.DS4432U) {
        if (core_voltage != 0.0f) {
            ESP_RETURN_ON_ERROR(DS4432U_set_voltage(core_voltage), TAG, "DS4432U set voltage failed!");
        }
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        uint16_t voltage_domains = GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains;
        ESP_RETURN_ON_ERROR(TPS546_set_vout(core_voltage * voltage_domains), TAG, "TPS546 set voltage failed!");
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

uint8_t VCORE_get_phase_count(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_phase_count();
    }
    return 1;
}
