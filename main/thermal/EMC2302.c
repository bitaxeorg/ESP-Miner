#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"

#include "i2c_bitaxe.h"
#include "EMC2302.h"

typedef enum {
    RNG_500_RPM = 0,
    RNG_1000_RPM,
    RNG_2000_RPM,
    RNG_4000_RPM,
} fan_config_range_t;

static const uint8_t TACH_MULTIPLIERS[] = {1, 2, 4, 8};

struct fan_config_t {
    uint8_t update_time : 3;
    uint8_t edges : 2;
    fan_config_range_t range: 2;
    uint8_t enable_closed_loop_algorithm : 1;
} __attribute__((packed));

static const char * TAG = "EMC2302";

static i2c_master_dev_handle_t EMC2302_dev_handle;
static uint8_t fan1_multiplier;
static uint8_t fan2_multiplier;

static esp_err_t set_fan_range(uint8_t reg_addr, fan_config_range_t range, uint8_t * multiplier)
{
    struct fan_config_t fan_config;
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_read(EMC2302_dev_handle, reg_addr, (uint8_t *)&fan_config, 1), TAG, "Failed to read fan configuration");

    fan_config.range = range;
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2302_dev_handle, reg_addr, *(uint8_t *)&fan_config), TAG, "Failed to write fan configuration");

    *multiplier = TACH_MULTIPLIERS[range];

    return ESP_OK;
}

/**
 * @brief Initialize the EMC2302 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t EMC2302_init()
{
    ESP_RETURN_ON_ERROR(i2c_bitaxe_add_device(EMC2302_I2CADDR_DEFAULT, &EMC2302_dev_handle, TAG), TAG, "Failed to add device");

    ESP_LOGI(TAG, "EMC2302 init");

    // Set the minimum fan speed measured and reported to 500 RPM
    ESP_RETURN_ON_ERROR(set_fan_range(EMC2302_FAN1_CONFIG1, RNG_500_RPM, &fan1_multiplier), TAG, "Failed to set fan 1 config");
    ESP_RETURN_ON_ERROR(set_fan_range(EMC2302_FAN2_CONFIG1, RNG_500_RPM, &fan2_multiplier), TAG, "Failed to set fan 2 config");

    return ESP_OK;
}

/**
 * @brief Set the fan speed as a percentage.
 *
 * @param percent The desired fan speed as a percentage (0.0 to 1.0).
 */
esp_err_t EMC2302_set_fan_speed(float percent)
{
    uint8_t setting = (uint8_t) (255.0 * percent);
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2302_dev_handle, EMC2302_FAN1_SETTING, setting), TAG, "Failed to set fan speed");
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2302_dev_handle, EMC2302_FAN2_SETTING, setting), TAG, "Failed to set fan speed");
    return ESP_OK;
}

static uint16_t get_fan_speed(uint8_t reg_addr, uint8_t multiplier)
{
    esp_err_t err;

    uint8_t tach_data[2];
    err = i2c_bitaxe_register_read(EMC2302_dev_handle, reg_addr, tach_data, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed: %s", esp_err_to_name(err));
        return 0;
    }
    
    if (tach_data[0] == 0xFF) {
        return 0;
    }

    uint16_t tach_counter = (tach_data[0] << 5) | (tach_data[1] >> 3);
    uint32_t rpm = 3932160UL * multiplier / tach_counter;

    if (rpm > UINT16_MAX) {
        ESP_LOGW(TAG, "RPM %u exceeds uint16_t range, clamping", rpm);
        rpm = UINT16_MAX;
    }

    return (uint16_t)rpm;
}

/**
 * @brief Get the current fan speed in RPM.
 *
 * @return uint16_t The fan speed in RPM.
 */
uint16_t EMC2302_get_fan_speed(void)
{
    return get_fan_speed(EMC2302_TACH1_LSB, fan1_multiplier);
}

/**
 * @brief Get the current fan 2 speed in RPM.
 *
 * @return uint16_t The fan 2 speed in RPM.
 */
uint16_t EMC2302_get_fan2_speed(void)
{
    return get_fan_speed(EMC2302_TACH2_LSB, fan2_multiplier);
}
