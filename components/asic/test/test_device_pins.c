#include <string.h>
#include "unity.h"
#include "device_config.h"
#include "sdkconfig.h"
#undef CONFIG_ENABLE_BAP
#define CONFIG_ENABLE_BAP 1
#undef CONFIG_GPIO_I2C_SDA
#define CONFIG_GPIO_I2C_SDA 12
#undef CONFIG_GPIO_I2C_SCL
#define CONFIG_GPIO_I2C_SCL 13
#include "../../../main/device_pins.c"

TEST_CASE("Bonanza control pins cannot conflict with the configured I2C bus", "[asic][bonanza][pins]")
{
    bzm_control_pins_t conflicting = BONANZA_1004_CONTROL_PINS;
    conflicting.trip = CONFIG_GPIO_I2C_SDA;
    DevicePins config = {.i2c = &DEFAULT_I2C_PINS, .bzm = &conflicting};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, device_pins_init(&config));
}

TEST_CASE("Bonanza 1004 supplies its control pin map through device configuration", "[asic][bonanza][pins]")
{
    DeviceConfig config = {0};
    for (unsigned i = 0; i < (sizeof(default_configs) / sizeof(default_configs[0])); ++i) {
        if (strcmp(default_configs[i].board_version, "1004") == 0) config = default_configs[i];
    }
    TEST_ASSERT_EQUAL_PTR(&BONANZA_1004_CONTROL_PINS, config.pins.bzm);
    const bzm_control_pins_t *pins = config.pins.bzm;
    TEST_ASSERT_EQUAL_INT(1, pins->reset);
    TEST_ASSERT_EQUAL_INT(2, pins->trip);
    TEST_ASSERT_EQUAL_INT(8, pins->fan_pwm);
    TEST_ASSERT_EQUAL_INT(9, pins->fan_tach);
    TEST_ASSERT_EQUAL_INT(10, pins->core_enable);
    TEST_ASSERT_EQUAL_INT(11, pins->pgood);
    TEST_ASSERT_EQUAL_INT(21, pins->five_volt_enable);
    TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&config.pins));
    for (unsigned i = 0; i < (sizeof(default_configs) / sizeof(default_configs[0])); ++i) {
        if (default_configs[i].family.id != BONANZA) TEST_ASSERT_NULL(default_configs[i].pins.bzm);
    }
}
