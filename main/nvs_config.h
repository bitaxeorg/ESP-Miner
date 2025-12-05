#ifndef MAIN_NVS_CONFIG_H
#define MAIN_NVS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    NVS_CONFIG_WIFI_SSID,
    NVS_CONFIG_WIFI_PASS,
    NVS_CONFIG_HOSTNAME,

    NVS_CONFIG_STRATUM_URL,
    NVS_CONFIG_STRATUM_PORT,
    NVS_CONFIG_STRATUM_USER,
    NVS_CONFIG_STRATUM_PASS,
    NVS_CONFIG_STRATUM_DIFFICULTY,
    NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE,
    NVS_CONFIG_STRATUM_TLS,
    NVS_CONFIG_STRATUM_CERT,
    NVS_CONFIG_FALLBACK_STRATUM_URL,
    NVS_CONFIG_FALLBACK_STRATUM_PORT,
    NVS_CONFIG_FALLBACK_STRATUM_USER,
    NVS_CONFIG_FALLBACK_STRATUM_PASS,
    NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY,
    NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE,
    NVS_CONFIG_FALLBACK_STRATUM_TLS,
    NVS_CONFIG_FALLBACK_STRATUM_CERT,
    NVS_CONFIG_USE_FALLBACK_STRATUM,
    
    NVS_CONFIG_ASIC_FREQUENCY,
    NVS_CONFIG_ASIC_VOLTAGE,
    NVS_CONFIG_OVERCLOCK_ENABLED,
    
    NVS_CONFIG_DISPLAY,
    NVS_CONFIG_ROTATION,
    NVS_CONFIG_INVERT_SCREEN,
    NVS_CONFIG_DISPLAY_TIMEOUT,
    NVS_CONFIG_DISPLAY_OFFSET,
    
    NVS_CONFIG_AUTO_FAN_SPEED,
    NVS_CONFIG_MANUAL_FAN_SPEED,
    NVS_CONFIG_MIN_FAN_SPEED,
    NVS_CONFIG_TEMP_TARGET,
    NVS_CONFIG_OVERHEAT_MODE,
    
    NVS_CONFIG_STATISTICS_FREQUENCY,
    
    NVS_CONFIG_BEST_DIFF,
    NVS_CONFIG_SELF_TEST,
    NVS_CONFIG_SWARM,
    NVS_CONFIG_THEME_SCHEME,
    NVS_CONFIG_THEME_COLORS,
    
    NVS_CONFIG_BOARD_VERSION,
    NVS_CONFIG_DEVICE_MODEL,
    NVS_CONFIG_ASIC_MODEL,

    NVS_CONFIG_PLUG_SENSE,
    NVS_CONFIG_ASIC_ENABLE,
    NVS_CONFIG_EMC2101,
    NVS_CONFIG_EMC2103,
    NVS_CONFIG_EMC2302,
    NVS_CONFIG_EMC_INTERNAL_TEMP,
    NVS_CONFIG_EMC_IDEALITY_FACTOR,
    NVS_CONFIG_EMC_BETA_COMPENSATION,
    NVS_CONFIG_TEMP_OFFSET,
    NVS_CONFIG_DS4432U,
    NVS_CONFIG_INA260,
    NVS_CONFIG_TPS546,
    NVS_CONFIG_TMP1075,
    NVS_CONFIG_POWER_CONSUMPTION_TARGET,
    NVS_CONFIG_COUNT
} NvsConfigKey;

typedef enum {
    TYPE_STR,
    TYPE_U16,
    TYPE_I32,
    TYPE_U64,
    TYPE_FLOAT,
    TYPE_BOOL
} ConfigType;

typedef union {
    char *str;
    uint16_t u16;
    int32_t i32;
    uint64_t u64;
    float f;
    bool b;
} ConfigValue;

typedef struct {
    const char *nvs_key_name;
    ConfigType type;
    ConfigValue value;
    ConfigValue default_value;
    const char *rest_name;
    int min;
    int max;
} Settings;

esp_err_t nvs_config_init(void);

char * nvs_config_get_string(NvsConfigKey key);
void nvs_config_set_string(NvsConfigKey key, const char * value);
uint16_t nvs_config_get_u16(NvsConfigKey key);
void nvs_config_set_u16(NvsConfigKey key, uint16_t value);
int32_t nvs_config_get_i32(NvsConfigKey key);
void nvs_config_set_i32(NvsConfigKey key, int32_t value);
uint64_t nvs_config_get_u64(NvsConfigKey key);
void nvs_config_set_u64(NvsConfigKey key, uint64_t value);
float nvs_config_get_float(NvsConfigKey key);
void nvs_config_set_float(NvsConfigKey key, float value);
bool nvs_config_get_bool(NvsConfigKey key);
void nvs_config_set_bool(NvsConfigKey key, bool value);
Settings *nvs_config_get_settings(NvsConfigKey key);

#endif // MAIN_NVS_CONFIG_H
