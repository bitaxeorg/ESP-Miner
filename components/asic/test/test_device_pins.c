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
#include "main/device_pins.c"

TEST_CASE("Bonanza control pins cannot conflict with the configured I2C bus", "[asic][bonanza][pins]")
{
    BzmPins conflicting = BONANZA_1004_CONTROL_PINS;
    conflicting.asic_trip = CONFIG_GPIO_I2C_SDA;
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
    TEST_ASSERT_EQUAL_PTR(&BONANZA_1004_UART_PINS, config.pins.asic_uart);
    TEST_ASSERT_EQUAL_PTR(&BONANZA_FAN_DRIVER, config.fan_driver);
    TEST_ASSERT_EQUAL_PTR(&BONANZA_VCORE_DRIVER, config.vcore_driver);
    TEST_ASSERT_EQUAL(GPIO_NUM_17, config.pins.asic_uart->tx);
    TEST_ASSERT_EQUAL(GPIO_NUM_18, config.pins.asic_uart->rx);
    const BzmPins *pins = config.pins.bzm;
    TEST_ASSERT_EQUAL_INT(1, pins->asic_reset);
    TEST_ASSERT_EQUAL_INT(2, pins->asic_trip);
    TEST_ASSERT_EQUAL_INT(8, pins->fan_pwm);
    TEST_ASSERT_EQUAL_INT(9, pins->fan_tach);
    TEST_ASSERT_EQUAL_INT(10, pins->core_enable);
    TEST_ASSERT_EQUAL_INT(11, pins->core_power_good);
    TEST_ASSERT_EQUAL_INT(21, pins->enable_5v);
    TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&config.pins));
    for (unsigned i = 0; i < (sizeof(default_configs) / sizeof(default_configs[0])); ++i) {
        if (default_configs[i].family.id != BONANZA) {
            TEST_ASSERT_NULL(default_configs[i].pins.bzm);
            TEST_ASSERT_NULL(default_configs[i].fan_driver);
            TEST_ASSERT_NULL(default_configs[i].vcore_driver);
        }
    }
}

TEST_CASE("Configured ASIC UART pins participate in board conflict validation", "[asic][bonanza][pins]")
{
    const UartPins invalid[] = {
        {.tx = GPIO_NUM_17, .rx = CONFIG_GPIO_I2C_SDA},
        {.tx = GPIO_NUM_17, .rx = GPIO_NUM_2},
        {.tx = GPIO_NUM_17, .rx = GPIO_NUM_17},
        {.tx = GPIO_NUM_NC, .rx = GPIO_NUM_18},
        {.tx = GPIO_NUM_17, .rx = GPIO_NUM_NC},
        {.tx = GPIO_NUM_MAX, .rx = GPIO_NUM_18},
        {.tx = GPIO_NUM_17, .rx = GPIO_NUM_MAX},
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        DevicePins config = {.i2c = &DEFAULT_I2C_PINS, .bzm = &BONANZA_1004_CONTROL_PINS, .asic_uart = &invalid[i]};
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, device_pins_init(&config));
    }
}

TEST_CASE("Existing Bitmain boards retain their legacy UART configuration", "[asic][pins]")
{
    for (unsigned i = 0; i < sizeof(default_configs) / sizeof(default_configs[0]); ++i) {
        DeviceConfig config = default_configs[i];
        if (config.family.asic.id == BZM) continue;
        TEST_ASSERT_NULL(config.pins.asic_uart);
        TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&config.pins));
    }
}
