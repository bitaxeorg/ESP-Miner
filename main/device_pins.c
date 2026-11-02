#include "device_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char * TAG = "device_pins";

const BapPins DEFAULT_BAP_PINS = {
    .tx = GPIO_NUM_39,
    .rx = GPIO_NUM_40,
};

const I2cPins DEFAULT_I2C_PINS = {
    .sda = GPIO_NUM_47,
    .scl = GPIO_NUM_48,
};

const I2cPins BITAXE_COLOR_I2C_PINS = {
    .sda = GPIO_NUM_44,
    .scl = GPIO_NUM_43,
};

const I80Pins ST7789_I80_PINS = {
    .data = { GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48 },
    .rd = GPIO_NUM_9,
    .pwr = GPIO_NUM_15,
    .wr = GPIO_NUM_8,
    .cs = GPIO_NUM_6,
    .dc = GPIO_NUM_7,
    .rst = GPIO_NUM_5,
    .bk_light = GPIO_NUM_38,
};

static inline esp_err_t check_pin(gpio_num_t gpio, const char * name, uint64_t * allocated_pins_mask)
{
    if (gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }
    if (!GPIO_IS_VALID_GPIO(gpio)) {
        ESP_LOGE(TAG, "INVALID GPIO: %d for %s is out of range (0-%d)!", gpio, name, GPIO_NUM_MAX - 1);
        return ESP_ERR_INVALID_ARG;
    }
    if (*allocated_pins_mask & (1ULL << gpio)) {
        ESP_LOGE(TAG, "PIN CONFLICT: GPIO %d (%s) is already in use by another interface!", gpio, name);
        return ESP_ERR_INVALID_STATE;
    }
    *allocated_pins_mask |= (1ULL << gpio);
    return ESP_OK;
}

static esp_err_t device_pins_validate(const DevicePins * pins)
{
    uint64_t allocated_pins_mask = 0;
    esp_err_t status = ESP_OK;

    if (pins->bap) {
        if (check_pin(pins->bap->tx, "BAP TX", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->bap->rx, "BAP RX", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
    }

    if (pins->i2c) {
        if (check_pin(pins->i2c->sda, "I2C SDA", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i2c->scl, "I2C SCL", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
    }

    if (pins->i80) {
        for (int i = 0; i < 8; i++) {
            if (check_pin(pins->i80->data[i], "LCD Data", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        }
        if (check_pin(pins->i80->rd, "LCD RD", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i80->wr, "LCD WR", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i80->cs, "LCD CS", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i80->dc, "LCD DC", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i80->rst, "LCD RST", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i80->pwr, "LCD PWR", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(pins->i80->bk_light, "LCD BK_LIGHT", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
    }

    return status;
}

static void apply_kconfig_pin_overrides(DevicePins * pins)
{
#if !defined(CONFIG_ENABLE_BAP)
    pins->bap = NULL;
    ESP_LOGI(TAG, "BAP disabled via Kconfig (CONFIG_ENABLE_BAP=n)");
#elif defined(CONFIG_GPIO_BAP_TX) && defined(CONFIG_GPIO_BAP_RX)
    if (CONFIG_GPIO_BAP_TX >= 0 && CONFIG_GPIO_BAP_RX >= 0) {
        static BapPins kconfig_bap_pins;
        kconfig_bap_pins.tx = CONFIG_GPIO_BAP_TX;
        kconfig_bap_pins.rx = CONFIG_GPIO_BAP_RX;
        pins->bap = &kconfig_bap_pins;
        ESP_LOGI(TAG, "Kconfig override applied for BAP pins: TX=%d RX=%d", CONFIG_GPIO_BAP_TX, CONFIG_GPIO_BAP_RX);
    }
#endif

#if defined(CONFIG_GPIO_I2C_SDA) && defined(CONFIG_GPIO_I2C_SCL)
    if (CONFIG_GPIO_I2C_SDA >= 0 && CONFIG_GPIO_I2C_SCL >= 0) {
        static I2cPins kconfig_i2c_pins;
        kconfig_i2c_pins.sda = CONFIG_GPIO_I2C_SDA;
        kconfig_i2c_pins.scl = CONFIG_GPIO_I2C_SCL;
        pins->i2c = &kconfig_i2c_pins;
        ESP_LOGI(TAG, "Kconfig override applied for I2C pins: SDA=%d SCL=%d", CONFIG_GPIO_I2C_SDA, CONFIG_GPIO_I2C_SCL);
    }
#endif
}

esp_err_t device_pins_init(DevicePins * pins)
{
    if (!pins) {
        return ESP_ERR_INVALID_ARG;
    }
    apply_kconfig_pin_overrides(pins);
    return device_pins_validate(pins);
}
