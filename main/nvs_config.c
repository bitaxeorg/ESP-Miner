#include "nvs_config.h"
#include <esp_err.h>
#include "esp_log.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "display.h"
#include "theme_api.h"

#define NVS_CONFIG_NAMESPACE "main"
#define NVS_STR_LIMIT (4000 - 1) // See nvs_set_str

#ifdef CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE
    #define STRATUM_EXTRANONCE_SUBSCRIBE 1
#else
    #define STRATUM_EXTRANONCE_SUBSCRIBE 0
#endif

#ifdef CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE
    #define FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE 1
#else
    #define FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE 0
#endif

typedef struct {
    NvsConfigKey key;
    ConfigType type;
    ConfigValue value;
} ConfigUpdate;

static const char * TAG = "nvs_config";

static QueueHandle_t nvs_save_queue = NULL;
static nvs_handle_t handle;

static Settings settings[NVS_CONFIG_COUNT] = {
    [NVS_CONFIG_WIFI_SSID]                             = {.nvs_key_name = "wifissid",        .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_ESP_WIFI_SSID},                .rest_name = "ssid",                               .min = 1,  .max = 32},
    [NVS_CONFIG_WIFI_PASS]                             = {.nvs_key_name = "wifipass",        .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_ESP_WIFI_PASSWORD},            .rest_name = "wifiPass",                           .min = 1,  .max = 63},
    [NVS_CONFIG_HOSTNAME]                              = {.nvs_key_name = "hostname",        .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_LWIP_LOCAL_HOSTNAME},          .rest_name = "hostname",                           .min = 1,  .max = 32},

    [NVS_CONFIG_STRATUM_URL]                           = {.nvs_key_name = "stratumurl",      .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_STRATUM_URL},                  .rest_name = "stratumURL",                         .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_STRATUM_PORT]                          = {.nvs_key_name = "stratumport",     .type = TYPE_U16,   .default_value = {.u16 = CONFIG_STRATUM_PORT},                         .rest_name = "stratumPort",                        .min = 0,  .max = UINT16_MAX},
    [NVS_CONFIG_STRATUM_USER]                          = {.nvs_key_name = "stratumuser",     .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_STRATUM_USER},                 .rest_name = "stratumUser",                        .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_STRATUM_PASS]                          = {.nvs_key_name = "stratumpass",     .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_STRATUM_PW},                   .rest_name = "stratumPassword",                    .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_STRATUM_DIFFICULTY]                    = {.nvs_key_name = "stratumdiff",     .type = TYPE_U16,   .default_value = {.u16 = CONFIG_STRATUM_DIFFICULTY},                   .rest_name = "stratumSuggestedDifficulty",         .min = 0,  .max = UINT16_MAX},
    [NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE]          = {.nvs_key_name = "stratumxnsub",    .type = TYPE_BOOL,  .default_value = {.b   = (bool)STRATUM_EXTRANONCE_SUBSCRIBE},          .rest_name = "stratumExtranonceSubscribe",         .min = 0,  .max = 1},
    [NVS_CONFIG_FALLBACK_STRATUM_URL]                  = {.nvs_key_name = "fbstratumurl",    .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_FALLBACK_STRATUM_URL},         .rest_name = "fallbackStratumURL",                 .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_FALLBACK_STRATUM_PORT]                 = {.nvs_key_name = "fbstratumport",   .type = TYPE_U16,   .default_value = {.u16 = CONFIG_FALLBACK_STRATUM_PORT},                .rest_name = "fallbackStratumPort",                .min = 0,  .max = UINT16_MAX},
    [NVS_CONFIG_FALLBACK_STRATUM_USER]                 = {.nvs_key_name = "fbstratumuser",   .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_FALLBACK_STRATUM_USER},        .rest_name = "fallbackStratumUser",                .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_FALLBACK_STRATUM_PASS]                 = {.nvs_key_name = "fbstratumpass",   .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_FALLBACK_STRATUM_PW},          .rest_name = "fallbackStratumPassword",            .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY]           = {.nvs_key_name = "fbstratumdiff",   .type = TYPE_U16,   .default_value = {.u16 = CONFIG_FALLBACK_STRATUM_DIFFICULTY},          .rest_name = "fallbackStratumSuggestedDifficulty", .min = 0,  .max = UINT16_MAX},
    [NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE] = {.nvs_key_name = "stratumfbxnsub",  .type = TYPE_BOOL,  .default_value = {.b   = (bool)FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE}, .rest_name = "fallbackStratumExtranonceSubscribe", .min = 0,  .max = 1},
    [NVS_CONFIG_USE_FALLBACK_STRATUM]                  = {.nvs_key_name = "usefbstartum",    .type = TYPE_BOOL,                                                                         .rest_name = "useFallbackStratum",                 .min = 0,  .max = 1},

    [NVS_CONFIG_ASIC_FREQUENCY]                        = {.nvs_key_name = "asicfrequency",   .type = TYPE_U16,   .default_value = {.u16 = CONFIG_ASIC_FREQUENCY}},
    [NVS_CONFIG_ASIC_FREQUENCY_FLOAT]                  = {.nvs_key_name = "asicfrequency_f", .type = TYPE_FLOAT, .default_value = {.f   = -1},                                          .rest_name = "frequency",                          .min = 1,  .max = UINT16_MAX},
    [NVS_CONFIG_ASIC_VOLTAGE]                          = {.nvs_key_name = "asicvoltage",     .type = TYPE_U16,   .default_value = {.u16 = CONFIG_ASIC_VOLTAGE},                         .rest_name = "coreVoltage",                        .min = 1,  .max = UINT16_MAX},
    [NVS_CONFIG_OVERCLOCK_ENABLED]                     = {.nvs_key_name = "oc_enabled",      .type = TYPE_BOOL,                                                                         .rest_name = "overclockEnabled",                   .min = 0,  .max = 1},
    
    [NVS_CONFIG_DISPLAY]                               = {.nvs_key_name = "display",         .type = TYPE_STR,   .default_value = {.str = DEFAULT_DISPLAY},                             .rest_name = "display",                            .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_ROTATION]                              = {.nvs_key_name = "rotation",        .type = TYPE_U16,                                                                          .rest_name = "rotation",                           .min = 0,  .max = 270},
    [NVS_CONFIG_INVERT_SCREEN]                         = {.nvs_key_name = "invertscreen",    .type = TYPE_BOOL,                                                                         .rest_name = "invertscreen",                       .min = 0,  .max = 1},
    [NVS_CONFIG_DISPLAY_OFFSET]                        = {.nvs_key_name = "displayOffset",   .type = TYPE_U16,   .default_value = {.u16 = LCD_SH1107_PARAM_DEFAULT_DISP_OFFSET },       .rest_name = "displayOffset",                      .min = 0,  .max = UINT8_MAX},
    [NVS_CONFIG_DISPLAY_TIMEOUT]                       = {.nvs_key_name = "displayTimeout",  .type = TYPE_I32,   .default_value = {.i32 = -1},                                          .rest_name = "displayTimeout",                     .min = -1, .max = UINT16_MAX},

    [NVS_CONFIG_AUTO_FAN_SPEED]                        = {.nvs_key_name = "autofanspeed",    .type = TYPE_BOOL,  .default_value = {.b   = true},                                        .rest_name = "autofanspeed",                       .min = 0,  .max = 1},
    [NVS_CONFIG_MANUAL_FAN_SPEED]                      = {.nvs_key_name = "fanspeed",        .type = TYPE_U16,   .default_value = {.u16 = 100},                                         .rest_name = "manualFanSpeed",                     .min = 0,  .max = 100},
    [NVS_CONFIG_MIN_FAN_SPEED]                         = {.nvs_key_name = "minfanspeed",     .type = TYPE_U16,   .default_value = {.u16 = 25},                                          .rest_name = "minFanSpeed",                        .min = 0,  .max = 99},
    [NVS_CONFIG_TEMP_TARGET]                           = {.nvs_key_name = "temptarget",      .type = TYPE_U16,   .default_value = {.u16 = 60},                                          .rest_name = "temptarget",                         .min = 35, .max = 66},
    [NVS_CONFIG_OVERHEAT_MODE]                         = {.nvs_key_name = "overheat_mode",   .type = TYPE_BOOL,                                                                         .rest_name = "overheat_mode",                      .min = 0,  .max = 0},

    [NVS_CONFIG_STATISTICS_FREQUENCY]                  = {.nvs_key_name = "statsFrequency",  .type = TYPE_U16,                                                                          .rest_name = "statsFrequency",                     .min = 0,  .max = UINT16_MAX},

    [NVS_CONFIG_BEST_DIFF]                             = {.nvs_key_name = "bestdiff",        .type = TYPE_U64},
    [NVS_CONFIG_SELF_TEST]                             = {.nvs_key_name = "selftest",        .type = TYPE_BOOL},
    [NVS_CONFIG_SWARM]                                 = {.nvs_key_name = "swarmconfig",     .type = TYPE_STR,   .default_value = {.str = ""}},
    [NVS_CONFIG_THEME_SCHEME]                          = {.nvs_key_name = "themescheme",     .type = TYPE_STR,   .default_value = {.str = DEFAULT_THEME}},
    [NVS_CONFIG_THEME_COLORS]                          = {.nvs_key_name = "themecolors",     .type = TYPE_STR,   .default_value = {.str = DEFAULT_COLORS}},
    
    [NVS_CONFIG_BOARD_VERSION]                         = {.nvs_key_name = "boardversion",    .type = TYPE_STR,   .default_value = {.str = "000"}},
    [NVS_CONFIG_DEVICE_MODEL]                          = {.nvs_key_name = "devicemodel",     .type = TYPE_STR,   .default_value = {.str = "unknown"}},
    [NVS_CONFIG_ASIC_MODEL]                            = {.nvs_key_name = "asicmodel",       .type = TYPE_STR,   .default_value = {.str = "unknown"}},
    [NVS_CONFIG_PLUG_SENSE]                            = {.nvs_key_name = "plug_sense",      .type = TYPE_BOOL},
    [NVS_CONFIG_ASIC_ENABLE]                           = {.nvs_key_name = "asic_enable",     .type = TYPE_BOOL},
    [NVS_CONFIG_EMC2101]                               = {.nvs_key_name = "EMC2101",         .type = TYPE_BOOL},
    [NVS_CONFIG_EMC2103]                               = {.nvs_key_name = "EMC2103",         .type = TYPE_BOOL},
    [NVS_CONFIG_EMC2302]                               = {.nvs_key_name = "EMC2302",         .type = TYPE_BOOL},
    [NVS_CONFIG_EMC_INTERNAL_TEMP]                     = {.nvs_key_name = "emc_int_temp",    .type = TYPE_BOOL},
    [NVS_CONFIG_EMC_IDEALITY_FACTOR]                   = {.nvs_key_name = "emc_ideality_f",  .type = TYPE_U16},
    [NVS_CONFIG_EMC_BETA_COMPENSATION]                 = {.nvs_key_name = "emc_beta_comp",   .type = TYPE_U16},
    [NVS_CONFIG_TEMP_OFFSET]                           = {.nvs_key_name = "temp_offset",     .type = TYPE_I32},
    [NVS_CONFIG_DS4432U]                               = {.nvs_key_name = "DS4432U",         .type = TYPE_BOOL},
    [NVS_CONFIG_INA260]                                = {.nvs_key_name = "INA260",          .type = TYPE_BOOL},
    [NVS_CONFIG_TPS546]                                = {.nvs_key_name = "TPS546",          .type = TYPE_BOOL},
    [NVS_CONFIG_TMP1075]                               = {.nvs_key_name = "TMP1075",         .type = TYPE_BOOL},
    [NVS_CONFIG_POWER_CONSUMPTION_TARGET]              = {.nvs_key_name = "power_cons_tgt",  .type = TYPE_U16},
};

Settings *nvs_config_get_settings(NvsConfigKey key)
{
    if (key < 0 || key >= NVS_CONFIG_COUNT) {
        ESP_LOGE(TAG, "Invalid key enum %d", key);
        return NULL;
    }
    return &settings[key];
}

static void nvs_task(void *pvParameters)
{
    while (1) {
        ConfigUpdate update;
        if (xQueueReceive(nvs_save_queue, &update, portMAX_DELAY) == pdTRUE) {
            Settings *setting = nvs_config_get_settings(update.key);
            if (setting && setting->type == update.type) {
                esp_err_t ret = ESP_OK;
                char *old_str = NULL;
                switch (update.type) {
                    case TYPE_STR:
                        old_str = setting->value.str;
                        setting->value.str = update.value.str;
                        ret = nvs_set_str(handle, setting->nvs_key_name, setting->value.str);
                        break;
                    case TYPE_U16:
                        setting->value.u16 = update.value.u16;
                        ret = nvs_set_u16(handle, setting->nvs_key_name, setting->value.u16);
                        break;
                    case TYPE_I32:
                        setting->value.i32 = update.value.i32;
                        ret = nvs_set_i32(handle, setting->nvs_key_name, setting->value.i32);
                        break;
                    case TYPE_U64:
                        setting->value.u64 = update.value.u64;
                        ret = nvs_set_u64(handle, setting->nvs_key_name, setting->value.u64);
                        break;
                    case TYPE_FLOAT:
                        setting->value.f = update.value.f;
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%f", setting->value.f);
                        ret = nvs_set_str(handle, setting->nvs_key_name, buf);
                        break;
                    case TYPE_BOOL:
                        setting->value.b = update.value.b;
                        ret = nvs_set_u16(handle, setting->nvs_key_name, setting->value.b ? 1 : 0);
                        break;
                }
                if (ret == ESP_OK) {
                    ret = nvs_commit(handle);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to commit data to NVS");
                    }
                }
                if (old_str) free(old_str);
            } 
            else if (update.type == TYPE_STR) {
                free(update.value.str);
            }
        }
    }
}

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return err;
    }

    // Load all
    for (NvsConfigKey i = 0; i < NVS_CONFIG_COUNT; i++) {
        Settings *setting = &settings[i];
        esp_err_t ret;
        switch (setting->type) {
            case TYPE_STR: {
                size_t len = 0;
                nvs_get_str(handle, setting->nvs_key_name, NULL, &len);
                char *buf = len > 0 ? malloc(len) : NULL;
                if (buf) {
                    ret = nvs_get_str(handle, setting->nvs_key_name, buf, &len);
                    setting->value.str = (ret == ESP_OK) ? buf : strdup(setting->default_value.str);
                    if (ret != ESP_OK) free(buf);
                } else {
                    setting->value.str = strdup(setting->default_value.str);
                }
                break;
            }
            case TYPE_U16: {
                uint16_t val;
                ret = nvs_get_u16(handle, setting->nvs_key_name, &val);
                setting->value.u16 = (ret == ESP_OK) ? val : setting->default_value.u16;
                break;
            }
            case TYPE_I32: {
                int32_t val;
                ret = nvs_get_i32(handle, setting->nvs_key_name, &val);
                setting->value.i32 = (ret == ESP_OK) ? val : setting->default_value.i32;
                break;
            }
            case TYPE_U64: {
                uint64_t val;
                ret = nvs_get_u64(handle, setting->nvs_key_name, &val);
                setting->value.u64 = (ret == ESP_OK) ? val : setting->default_value.u64;
                break;
            }
            case TYPE_FLOAT: {
                char buf[32];
                size_t len = sizeof(buf);
                ret = nvs_get_str(handle, setting->nvs_key_name, buf, &len);
                setting->value.f = (ret == ESP_OK) ? atof(buf) : setting->default_value.f;
                break;
            }
            case TYPE_BOOL: {
                uint16_t val;
                ret = nvs_get_u16(handle, setting->nvs_key_name, &val);
                setting->value.b = (ret == ESP_OK) ? (val != 0) : setting->default_value.b;
                break;
            }
        }
    }

    nvs_save_queue = xQueueCreate(20, sizeof(ConfigUpdate));

    TaskHandle_t task_handle;

    // nvs_task heap _must_ be internal memory
    BaseType_t task_result = xTaskCreate(nvs_task, "nvs_task", 8192, NULL, 5, &task_handle); 
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create nvs_task");

        return ESP_FAIL;
    }
    return ESP_OK;
}

char *nvs_config_get_string(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_STR) {
        ESP_LOGE(TAG, "Wrong type for %s (str)", setting->nvs_key_name);
        return NULL;
    }
    return strdup(setting->value.str);
}

void nvs_config_set_string(NvsConfigKey key, const char *value)
{
    ConfigUpdate update = { .key = key, .type = TYPE_STR, .value.str = strdup(value) };
    if (!update.value.str) return;
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

uint16_t nvs_config_get_u16(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_U16) {
        ESP_LOGE(TAG, "Wrong type for %s (u16)", setting->nvs_key_name);
        return 0;
    }
    return setting->value.u16;
}

void nvs_config_set_u16(NvsConfigKey key, uint16_t value)
{
    ConfigUpdate update = { .key = key, .type = TYPE_U16, .value.u16 = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

int32_t nvs_config_get_i32(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_I32) {
        ESP_LOGE(TAG, "Wrong type for %s (i32)", setting->nvs_key_name);
        return 0;
    }
    return setting->value.i32;
}

void nvs_config_set_i32(NvsConfigKey key, int32_t value)
{
    ConfigUpdate update = { .key = key, .type = TYPE_I32, .value.i32 = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

uint64_t nvs_config_get_u64(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_U64) {
        ESP_LOGE(TAG, "Wrong type for %s (u64)", setting->nvs_key_name);
        return 0;
    }
    return setting->value.u64;
}

void nvs_config_set_u64(NvsConfigKey key, uint64_t value)
{
    ConfigUpdate update = { .key = key, .type = TYPE_U64, .value.u64 = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

float nvs_config_get_float(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_FLOAT) {
        ESP_LOGE(TAG, "Wrong type for %s (float)", setting->nvs_key_name);
        return 0;
    }
    return setting->value.f;
}

void nvs_config_set_float(NvsConfigKey key, float value)
{
    ConfigUpdate update = { .key = key, .type = TYPE_FLOAT, .value.f = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}


bool nvs_config_get_bool(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_BOOL) {
        ESP_LOGE(TAG, "Wrong type for %s (bool)", setting->nvs_key_name);
        return false;
    }
    return setting->value.b;
}

void nvs_config_set_bool(NvsConfigKey key, bool value)
{
    ConfigUpdate update = { .key = key, .type = TYPE_BOOL, .value.b = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}
