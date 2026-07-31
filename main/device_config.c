#include <string.h>
#include "device_config.h"
#include "nvs_config.h"
#include "global_state.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char * TAG = "device_config";

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

static esp_err_t device_config_validate_pins(const DeviceConfig * cfg)
{
    uint64_t allocated_pins_mask = 0;
    esp_err_t status = ESP_OK;

    if (cfg->bap_pins) {
        if (check_pin(cfg->bap_pins->tx, "BAP TX", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->bap_pins->rx, "BAP RX", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
    }

    if (cfg->i2c_pins) {
        if (check_pin(cfg->i2c_pins->sda, "I2C SDA", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i2c_pins->scl, "I2C SCL", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
    }

    if (cfg->i80_pins) {
        for (int i = 0; i < 8; i++) {
            if (check_pin(cfg->i80_pins->data[i], "LCD Data", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        }
        if (check_pin(cfg->i80_pins->rd, "LCD RD", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i80_pins->wr, "LCD WR", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i80_pins->cs, "LCD CS", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i80_pins->dc, "LCD DC", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i80_pins->rst, "LCD RST", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i80_pins->pwr, "LCD PWR", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
        if (check_pin(cfg->i80_pins->bk_light, "LCD BK_LIGHT", &allocated_pins_mask) != ESP_OK) status = ESP_ERR_INVALID_STATE;
    }

    return status;
}

static void apply_kconfig_pin_overrides(DeviceConfig * cfg)
{
#if defined(CONFIG_ENABLE_BAP) && !CONFIG_ENABLE_BAP
    cfg->bap_pins = NULL;
    ESP_LOGI(TAG, "BAP disabled via Kconfig (CONFIG_ENABLE_BAP=n)");
#elif defined(CONFIG_GPIO_BAP_TX) && defined(CONFIG_GPIO_BAP_RX)
    if (CONFIG_GPIO_BAP_TX >= 0 && CONFIG_GPIO_BAP_RX >= 0) {
        static BapPins kconfig_bap_pins;
        kconfig_bap_pins.tx = CONFIG_GPIO_BAP_TX;
        kconfig_bap_pins.rx = CONFIG_GPIO_BAP_RX;
        cfg->bap_pins = &kconfig_bap_pins;
        ESP_LOGI(TAG, "Kconfig override applied for BAP pins: TX=%d RX=%d", CONFIG_GPIO_BAP_TX, CONFIG_GPIO_BAP_RX);
    }
#endif

#if defined(CONFIG_GPIO_I2C_SDA) && defined(CONFIG_GPIO_I2C_SCL)
    if (CONFIG_GPIO_I2C_SDA >= 0 && CONFIG_GPIO_I2C_SCL >= 0) {
        static I2cPins kconfig_i2c_pins;
        kconfig_i2c_pins.sda = CONFIG_GPIO_I2C_SDA;
        kconfig_i2c_pins.scl = CONFIG_GPIO_I2C_SCL;
        cfg->i2c_pins = &kconfig_i2c_pins;
        ESP_LOGI(TAG, "Kconfig override applied for I2C pins: SDA=%d SCL=%d", CONFIG_GPIO_I2C_SDA, CONFIG_GPIO_I2C_SCL);
    }
#endif
}

esp_err_t device_config_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    // TODO: Read board version from eFuse

    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION);

    for (int i = 0 ; i < ARRAY_SIZE(default_configs); i++) {
        if (strcmp(default_configs[i].board_version, board_version) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG = default_configs[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
            ESP_LOGI(TAG, "Board Version: %s", GLOBAL_STATE->DEVICE_CONFIG.board_version);
            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            free(board_version);
            apply_kconfig_pin_overrides(&GLOBAL_STATE->DEVICE_CONFIG);
            return device_config_validate_pins(&GLOBAL_STATE->DEVICE_CONFIG);
        }
    }

    ESP_LOGI(TAG, "Custom Board Version: %s", board_version);

    GLOBAL_STATE->DEVICE_CONFIG.board_version = strdup(board_version);

    char * device_model = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL);

    for (int i = 0 ; i < ARRAY_SIZE(default_families); i++) {
        if (strcasecmp(default_families[i].name, device_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family = default_families[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);

            break;
        }
    }

    char * asic_model = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL);

    for (int i = 0 ; i < ARRAY_SIZE(default_asic_configs); i++) {
        if (strcasecmp(default_asic_configs[i].name, asic_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family.asic = default_asic_configs[i];

            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            break;
        }
    }

    GLOBAL_STATE->DEVICE_CONFIG.plug_sense = nvs_config_get_bool(NVS_CONFIG_PLUG_SENSE);
    GLOBAL_STATE->DEVICE_CONFIG.asic_enable = nvs_config_get_bool(NVS_CONFIG_ASIC_ENABLE);
    GLOBAL_STATE->DEVICE_CONFIG.EMC2101 = nvs_config_get_bool(NVS_CONFIG_EMC2101);
    GLOBAL_STATE->DEVICE_CONFIG.EMC2103 = nvs_config_get_bool(NVS_CONFIG_EMC2103);
    GLOBAL_STATE->DEVICE_CONFIG.EMC2302 = nvs_config_get_bool(NVS_CONFIG_EMC2302);
    GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp = nvs_config_get_bool(NVS_CONFIG_EMC_INTERNAL_TEMP);
    GLOBAL_STATE->DEVICE_CONFIG.emc_ideality_factor = nvs_config_get_u16(NVS_CONFIG_EMC_IDEALITY_FACTOR);
    GLOBAL_STATE->DEVICE_CONFIG.emc_beta_compensation = nvs_config_get_u16(NVS_CONFIG_EMC_BETA_COMPENSATION);
    GLOBAL_STATE->DEVICE_CONFIG.temp_offset = nvs_config_get_i32(NVS_CONFIG_TEMP_OFFSET);
    GLOBAL_STATE->DEVICE_CONFIG.DS4432U = nvs_config_get_bool(NVS_CONFIG_DS4432U);
    GLOBAL_STATE->DEVICE_CONFIG.INA260 = nvs_config_get_bool(NVS_CONFIG_INA260);
    GLOBAL_STATE->DEVICE_CONFIG.TPS546 = nvs_config_get_bool(NVS_CONFIG_TPS546);
    GLOBAL_STATE->DEVICE_CONFIG.TMP1075 = nvs_config_get_bool(NVS_CONFIG_TMP1075);

    // test values
    GLOBAL_STATE->DEVICE_CONFIG.power_consumption_target = nvs_config_get_u16(NVS_CONFIG_POWER_CONSUMPTION_TARGET);

    free(board_version);
    free(device_model);
    free(asic_model);

    apply_kconfig_pin_overrides(&GLOBAL_STATE->DEVICE_CONFIG);
    return device_config_validate_pins(&GLOBAL_STATE->DEVICE_CONFIG);
}
