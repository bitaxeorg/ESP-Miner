#include "nvs_config.h"
#include "sv2_protocol.h"
#include "global_state.h"
#include "cJSON.h"
#include <esp_err.h>
#include "esp_log.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "display.h"
#include "theme_api.h"
#include "scoreboard.h"
#include "cJSON.h"
#include "utils.h"

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

#define FALLBACK_KEY_ASICFREQUENCY "asicfrequency" // Since v2.10.0 (https://github.com/bitaxeorg/ESP-Miner/pull/1051)
#define FALLBACK_KEY_FANSPEED "fanspeed"           // Since v2.11.0 (https://github.com/bitaxeorg/ESP-Miner/pull/1331)

typedef struct {
    NvsConfigKey key;
    ConfigType type;
    ConfigValue value;
    int index;
} ConfigUpdate;

static const char * TAG = "nvs_config";

static QueueHandle_t nvs_save_queue = NULL;
static nvs_handle_t handle;
static SemaphoreHandle_t nvs_cache_mutex = NULL;

static Settings settings[NVS_CONFIG_COUNT] = {
    [NVS_CONFIG_WIFI_SSID]                             = {.nvs_key_name = "wifissid",        .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_ESP_WIFI_SSID},                .rest_name = "ssid",                               .min = 1,  .max = 32},
    [NVS_CONFIG_WIFI_PASS]                             = {.nvs_key_name = "wifipass",        .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_ESP_WIFI_PASSWORD},            .rest_name = "wifiPass",                           .min = 0,  .max = 63},
    [NVS_CONFIG_HOSTNAME]                              = {.nvs_key_name = "hostname",        .type = TYPE_STR,   .default_value = {.str = (char *)CONFIG_LWIP_LOCAL_HOSTNAME},          .rest_name = "hostname",                           .min = 1,  .max = 32},

    [NVS_CONFIG_POOL]                                  = {.nvs_key_name = "pool",            .type = TYPE_STR,   .default_value = {.str = ""},                                          .rest_name = "pools",                              .min = 0,  .max = NVS_STR_LIMIT, .array_size = MAX_POOLS},
    [NVS_CONFIG_PRIMARY_POOL_INDEX]                    = {.nvs_key_name = "prim_idx",        .type = TYPE_U16,   .default_value = {.u16 = 0},                                           .rest_name = "primaryPoolIndex",                   .min = 0,  .max = MAX_POOLS - 1},
    [NVS_CONFIG_SECONDARY_POOL_INDEX]                  = {.nvs_key_name = "sec_idx",         .type = TYPE_U16,   .default_value = {.u16 = 1},                                           .rest_name = "secondaryPoolIndex",                 .min = 0,  .max = MAX_POOLS - 1},
    [NVS_CONFIG_USE_FALLBACK_STRATUM]                  = {.nvs_key_name = "usefbstartum",    .type = TYPE_BOOL,  .default_value = {.b = true},                                          .rest_name = "useFallbackStratum",                 .min = 0,  .max = 1},

    [NVS_CONFIG_ASIC_FREQUENCY]                        = {.nvs_key_name = "asicfrequency_f", .type = TYPE_FLOAT, .default_value = {.f   = CONFIG_ASIC_FREQUENCY},                       .rest_name = "frequency",                          .min = 1,  .max = UINT16_MAX},
    [NVS_CONFIG_ASIC_VOLTAGE]                          = {.nvs_key_name = "asicvoltage",     .type = TYPE_U16,   .default_value = {.u16 = CONFIG_ASIC_VOLTAGE},                         .rest_name = "coreVoltage",                        .min = 1,  .max = UINT16_MAX},
    [NVS_CONFIG_OVERCLOCK_ENABLED]                     = {.nvs_key_name = "oc_enabled",      .type = TYPE_BOOL,                                                                         .rest_name = "overclockEnabled",                   .min = 0,  .max = 1},
    
    [NVS_CONFIG_DISPLAY]                               = {.nvs_key_name = "display",         .type = TYPE_STR,   .default_value = {.str = DEFAULT_DISPLAY},                             .rest_name = "display",                            .min = 0,  .max = NVS_STR_LIMIT},
    [NVS_CONFIG_ROTATION]                              = {.nvs_key_name = "rotation",        .type = TYPE_U16,                                                                          .rest_name = "rotation",                           .min = 0,  .max = 270},
    [NVS_CONFIG_INVERT_SCREEN]                         = {.nvs_key_name = "invertscreen",    .type = TYPE_BOOL,                                                                         .rest_name = "invertscreen",                       .min = 0,  .max = 1},
    [NVS_CONFIG_DISPLAY_OFFSET]                        = {.nvs_key_name = "displayOffset",   .type = TYPE_U16,   .default_value = {.u16 = LCD_SH1107_PARAM_DEFAULT_DISP_OFFSET },       .rest_name = "displayOffset",                      .min = 0,  .max = UINT8_MAX},
    [NVS_CONFIG_DISPLAY_TIMEOUT]                       = {.nvs_key_name = "displayTimeout",  .type = TYPE_I32,   .default_value = {.i32 = -1},                                          .rest_name = "displayTimeout",                     .min = -1, .max = UINT16_MAX},

    [NVS_CONFIG_AUTO_FAN_SPEED]                        = {.nvs_key_name = "autofanspeed",    .type = TYPE_BOOL,  .default_value = {.b   = true},                                        .rest_name = "autofanspeed",                       .min = 0,  .max = 1},
    [NVS_CONFIG_MANUAL_FAN_SPEED]                      = {.nvs_key_name = "manualfanspeed",  .type = TYPE_U16,   .default_value = {.u16 = 100},                                         .rest_name = "manualFanSpeed",                     .min = 0,  .max = 100},
    [NVS_CONFIG_MIN_FAN_SPEED]                         = {.nvs_key_name = "minfanspeed",     .type = TYPE_U16,   .default_value = {.u16 = 25},                                          .rest_name = "minFanSpeed",                        .min = 0,  .max = 99},
    [NVS_CONFIG_TEMP_TARGET]                           = {.nvs_key_name = "temptarget",      .type = TYPE_U16,   .default_value = {.u16 = 60},                                          .rest_name = "temptarget",                         .min = 35, .max = 66},
    [NVS_CONFIG_OVERHEAT_MODE]                         = {.nvs_key_name = "overheat_mode",   .type = TYPE_BOOL,                                                                         .rest_name = "overheat_mode",                      .min = 0,  .max = 0},

    [NVS_CONFIG_STATISTICS_FREQUENCY]                  = {.nvs_key_name = "statsFrequency",  .type = TYPE_U16,                                                                          .rest_name = "statsFrequency",                     .min = 0,  .max = UINT16_MAX},

    [NVS_CONFIG_BEST_DIFF]                             = {.nvs_key_name = "bestdiff",        .type = TYPE_U64},
    [NVS_CONFIG_SELF_TEST]                             = {.nvs_key_name = "selftest",        .type = TYPE_BOOL},
    [NVS_CONFIG_SWARM]                                 = {.nvs_key_name = "swarmconfig",     .type = TYPE_STR},
    [NVS_CONFIG_THEME_SCHEME]                          = {.nvs_key_name = "themescheme",     .type = TYPE_STR,   .default_value = {.str = DEFAULT_THEME}},
    [NVS_CONFIG_THEME_COLOR]                           = {.nvs_key_name = "themecolor",      .type = TYPE_STR,   .default_value = {.str = DEFAULT_COLOR}},
    [NVS_CONFIG_SCOREBOARD]                            = {.nvs_key_name = "scoreboard",      .type = TYPE_STR,   .array_size = MAX_SCOREBOARD},
    
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
    [NVS_CONFIG_SELF_TEST_TEMP_TARGET]                 = {.nvs_key_name = "selftest_temp",   .type = TYPE_U16,   .default_value = {.u16 = 65}},
    [NVS_CONFIG_SELF_TEST_TEMP_WARMUP]                 = {.nvs_key_name = "selftest_warm",   .type = TYPE_U16,   .default_value = {.u16 = 55}},
    [NVS_CONFIG_SELF_TEST_TEMP_MAX]                    = {.nvs_key_name = "selftest_max",    .type = TYPE_U16,   .default_value = {.u16 = 70}},
    [NVS_CONFIG_SELF_TEST_FAN_SPEED]                   = {.nvs_key_name = "selftest_fan",    .type = TYPE_U16,   .default_value = {.u16 = 1000}},
};

Settings *nvs_config_get_settings(NvsConfigKey key)
{
    if (key < 0 || key >= NVS_CONFIG_COUNT) {
        ESP_LOGE(TAG, "Invalid key enum %d", key);
        return NULL;
    }
    return &settings[key];
}

static int get_array_size(const Settings * setting)
{
    return (setting->array_size > 0) ? setting->array_size : 1;
}

static void get_nvs_key_name(const Settings * setting, const int index, char dest[static NVS_KEY_NAME_MAX_SIZE])
{
    if (setting->array_size > 0) {
        int width = 1;
        for (int t = setting->array_size - 1; t >= 10 && width < 5; t /= 10) width++;
        snprintf(dest, NVS_KEY_NAME_MAX_SIZE, "%s_%0*d", setting->nvs_key_name, width, index + 1);
    } else {
        snprintf(dest, NVS_KEY_NAME_MAX_SIZE, "%s", setting->nvs_key_name);
    }
}

static char* read_legacy_str(nvs_handle_t h, const char *key, const char *def) {
    size_t len = 0;
    if (nvs_get_str(h, key, NULL, &len) == ESP_OK && len > 0) {
        char *buf = malloc(len);
        if (buf) {
            if (nvs_get_str(h, key, buf, &len) == ESP_OK) {
                return buf;
            }
            free(buf);
        }
    }
    return strdup(def ? def : "");
}

static uint16_t read_legacy_u16(nvs_handle_t h, const char *key, uint16_t def) {
    uint16_t val;
    if (nvs_get_u16(h, key, &val) == ESP_OK) {
        return val;
    }
    return def;
}

static bool read_legacy_bool(nvs_handle_t h, const char *key, bool def) {
    uint16_t val;
    if (nvs_get_u16(h, key, &val) == ESP_OK) {
        return val != 0;
    }
    return def;
}

static void migrate_legacy_pools(void) {
    size_t len = 0;
    // Check if new configuration already exists
    if (nvs_get_str(handle, "pool_1", NULL, &len) == ESP_OK) {
        return; // Already migrated
    }

    ESP_LOGI(TAG, "Migrating legacy NVS pool configurations...");

    // Migrate Primary Pool -> pool_1
    char *p_proto = read_legacy_str(handle, "stratumprot", STRATUM_V1);
    char *p_url = read_legacy_str(handle, "stratumurl", CONFIG_STRATUM_URL);
    uint16_t p_port = read_legacy_u16(handle, "stratumport", CONFIG_STRATUM_PORT);
    char *p_user = read_legacy_str(handle, "stratumuser", CONFIG_STRATUM_USER);
    char *p_pass = read_legacy_str(handle, "stratumpass", CONFIG_STRATUM_PW);
    uint16_t p_diff = read_legacy_u16(handle, "stratumdiff", CONFIG_STRATUM_DIFFICULTY);
    bool p_xnsub = read_legacy_bool(handle, "stratumxnsub", STRATUM_EXTRANONCE_SUBSCRIBE);
    uint16_t p_tls = read_legacy_u16(handle, "stratumtls", CONFIG_STRATUM_TLS);
    char *p_cert = read_legacy_str(handle, "stratumcert", CONFIG_STRATUM_CERT);
    char *p_sv2chan = read_legacy_str(handle, "sv2chantype", SV2_CHANNEL_TYPE_EXTENDED);
    char *p_sv2pubkey = read_legacy_str(handle, "sv2authpubkey", "");
    bool p_decode = read_legacy_bool(handle, "stratumdecode", true);

    cJSON *p_json = cJSON_CreateObject();
    cJSON_AddStringToObject(p_json, "stratumProtocol", p_proto);
    cJSON_AddStringToObject(p_json, "stratumURL", p_url);
    cJSON_AddNumberToObject(p_json, "stratumPort", p_port);
    cJSON_AddStringToObject(p_json, "stratumUser", p_user);
    cJSON_AddStringToObject(p_json, "stratumPassword", p_pass);
    cJSON_AddNumberToObject(p_json, "stratumSuggestedDifficulty", p_diff);
    cJSON_AddBoolToObject(p_json, "stratumExtranonceSubscribe", p_xnsub);
    cJSON_AddNumberToObject(p_json, "stratumTLS", p_tls);
    cJSON_AddStringToObject(p_json, "stratumCert", p_cert);
    cJSON_AddStringToObject(p_json, "stratumV2ChannelType", p_sv2chan);
    cJSON_AddStringToObject(p_json, "stratumV2AuthorityPubkey", p_sv2pubkey);
    cJSON_AddBoolToObject(p_json, "stratumDecodeCoinbase", p_decode);

    char *p_str = cJSON_PrintUnformatted(p_json);
    if (p_str) {
        nvs_set_str(handle, "pool_1", p_str);
        free(p_str);
    }
    cJSON_Delete(p_json);
    free(p_proto); free(p_url); free(p_user); free(p_pass); free(p_cert); free(p_sv2chan); free(p_sv2pubkey);

    // Migrate Fallback Pool -> pool_2
    char *f_proto = read_legacy_str(handle, "fbstratumprot", STRATUM_V1);
    char *f_url = read_legacy_str(handle, "fbstratumurl", CONFIG_FALLBACK_STRATUM_URL);
    uint16_t f_port = read_legacy_u16(handle, "fbstratumport", CONFIG_FALLBACK_STRATUM_PORT);
    char *f_user = read_legacy_str(handle, "fbstratumuser", CONFIG_FALLBACK_STRATUM_USER);
    char *f_pass = read_legacy_str(handle, "fbstratumpass", CONFIG_FALLBACK_STRATUM_PW);
    uint16_t f_diff = read_legacy_u16(handle, "fbstratumdiff", CONFIG_FALLBACK_STRATUM_DIFFICULTY);
    bool f_xnsub = read_legacy_bool(handle, "stratumfbxnsub", FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);
    uint16_t f_tls = read_legacy_u16(handle, "fbstratumtls", CONFIG_FALLBACK_STRATUM_TLS);
    char *f_cert = read_legacy_str(handle, "fbstratumcert", CONFIG_FALLBACK_STRATUM_CERT);
    char *f_sv2chan = read_legacy_str(handle, "fbsv2chantype", SV2_CHANNEL_TYPE_EXTENDED);
    char *f_sv2pubkey = read_legacy_str(handle, "fbsv2authpubk", "");
    bool f_decode = read_legacy_bool(handle, "fbstratumdecode", true);

    cJSON *f_json = cJSON_CreateObject();
    cJSON_AddStringToObject(f_json, "stratumProtocol", f_proto);
    cJSON_AddStringToObject(f_json, "stratumURL", f_url);
    cJSON_AddNumberToObject(f_json, "stratumPort", f_port);
    cJSON_AddStringToObject(f_json, "stratumUser", f_user);
    cJSON_AddStringToObject(f_json, "stratumPassword", f_pass);
    cJSON_AddNumberToObject(f_json, "stratumSuggestedDifficulty", f_diff);
    cJSON_AddBoolToObject(f_json, "stratumExtranonceSubscribe", f_xnsub);
    cJSON_AddNumberToObject(f_json, "stratumTLS", f_tls);
    cJSON_AddStringToObject(f_json, "stratumCert", f_cert);
    cJSON_AddStringToObject(f_json, "stratumV2ChannelType", f_sv2chan);
    cJSON_AddStringToObject(f_json, "stratumV2AuthorityPubkey", f_sv2pubkey);
    cJSON_AddBoolToObject(f_json, "stratumDecodeCoinbase", f_decode);

    char *f_str = cJSON_PrintUnformatted(f_json);
    if (f_str) {
        nvs_set_str(handle, "pool_2", f_str);
        free(f_str);
    }
    cJSON_Delete(f_json);
    free(f_proto); free(f_url); free(f_user); free(f_pass); free(f_cert); free(f_sv2chan); free(f_sv2pubkey);

    // Set default selectors: primary = 0, fallback = 1
    nvs_set_u16(handle, "prim_idx", 0);
    nvs_set_u16(handle, "sec_idx", 1);
    nvs_commit(handle);

    ESP_LOGI(TAG, "Legacy pool migration completed successfully");
}

static void nvs_config_init_fallback(NvsConfigKey key, Settings * setting)
{
    if (key == NVS_CONFIG_POOL) {
        migrate_legacy_pools();
    }
    if (key == NVS_CONFIG_ASIC_FREQUENCY) {
        if (nvs_find_key(handle, setting->nvs_key_name, NULL) == ESP_ERR_NVS_NOT_FOUND) {
            uint16_t val = read_legacy_u16(handle, FALLBACK_KEY_ASICFREQUENCY, 0);
            if (val > 0) {
                ESP_LOGI(TAG, "Migrating NVS config %s to %s (%d)", FALLBACK_KEY_ASICFREQUENCY, setting->nvs_key_name, val);
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", val);
                nvs_set_str(handle, setting->nvs_key_name, buf);
            }
        }
    }
    if (key == NVS_CONFIG_MANUAL_FAN_SPEED) {
        if (nvs_find_key(handle, setting->nvs_key_name, NULL) == ESP_ERR_NVS_NOT_FOUND) {
            uint16_t val = read_legacy_u16(handle, FALLBACK_KEY_FANSPEED, 0);
            if (val > 0) {
                ESP_LOGI(TAG, "Migrating NVS config %s to %s (%d)", FALLBACK_KEY_FANSPEED, setting->nvs_key_name, val);
                nvs_set_u16(handle, setting->nvs_key_name, val);
            }
        }
    }
    if (key == NVS_CONFIG_THEME_COLOR) {
        if (nvs_find_key(handle, setting->nvs_key_name, NULL) == ESP_ERR_NVS_NOT_FOUND) {
            size_t len = 0;
            esp_err_t ret = nvs_get_str(handle, "themecolors", NULL, &len);
            if (ret == ESP_OK && len > 1) {
                char *buf = malloc(len);
                if (buf) {
                    ret = nvs_get_str(handle, "themecolors", buf, &len);
                    if (ret == ESP_OK) {
                        cJSON *root = cJSON_Parse(buf);
                        if (root) {
                            cJSON *primary = cJSON_GetObjectItem(root, "--primary-color");
                            if (primary && primary->valuestring) {
                                ESP_LOGI(TAG, "Migrating NVS config themecolors to %s (%s)", setting->nvs_key_name, primary->valuestring);
                                nvs_set_str(handle, setting->nvs_key_name, primary->valuestring);
                            }
                            cJSON_Delete(root);
                        }
                    }
                    free(buf);
                }
            }
        }
    }
}

static void nvs_config_apply_fallback(NvsConfigKey key, Settings * setting)
{
    if (key == NVS_CONFIG_ASIC_FREQUENCY) {
        nvs_set_u16(handle, FALLBACK_KEY_ASICFREQUENCY, (uint16_t) setting->value[0].f);
    }
    if (key == NVS_CONFIG_MANUAL_FAN_SPEED) {
        nvs_set_u16(handle, FALLBACK_KEY_FANSPEED, setting->value[0].u16);
    }
}

static void nvs_task(void *pvParameters)
{
    while (1) {
        ConfigUpdate update;
        if (xQueueReceive(nvs_save_queue, &update, portMAX_DELAY) == pdTRUE) {
            Settings *setting = nvs_config_get_settings(update.key);
            if (setting && setting->type == update.type) {
                esp_err_t ret = ESP_OK;

                char key[NVS_KEY_NAME_MAX_SIZE];
                get_nvs_key_name(setting, update.index, key);

                // NVS flash write is AFTER releasing the mutex so getters are never blocked
                char *old_str = NULL;
                char nvs_str_buf[32]; // for TYPE_FLOAT serialisation
                xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
                switch (update.type) {
                    case TYPE_STR:
                        old_str = setting->value[update.index].str;
                        setting->value[update.index].str = update.value.str;
                        break;
                    case TYPE_U16:
                        setting->value[update.index].u16 = update.value.u16;
                        break;
                    case TYPE_I32:
                        setting->value[update.index].i32 = update.value.i32;
                        break;
                    case TYPE_U64:
                        setting->value[update.index].u64 = update.value.u64;
                        break;
                    case TYPE_FLOAT:
                        setting->value[update.index].f = update.value.f;
                        snprintf(nvs_str_buf, sizeof(nvs_str_buf), "%f", update.value.f);
                        break;
                    case TYPE_BOOL:
                        setting->value[update.index].b = update.value.b;
                        break;
                }
                xSemaphoreGive(nvs_cache_mutex);

                switch (update.type) {
                    case TYPE_STR:
                        ret = nvs_set_str(handle, key, update.value.str);
                        break;
                    case TYPE_U16:
                        ret = nvs_set_u16(handle, key, update.value.u16);
                        break;
                    case TYPE_I32:
                        ret = nvs_set_i32(handle, key, update.value.i32);
                        break;
                    case TYPE_U64:
                        ret = nvs_set_u64(handle, key, update.value.u64);
                        break;
                    case TYPE_FLOAT:
                        ret = nvs_set_str(handle, key, nvs_str_buf);
                        break;
                    case TYPE_BOOL:
                        ret = nvs_set_u16(handle, key, update.value.b ? 1 : 0);
                        break;
                }

                nvs_config_apply_fallback(update.key, setting);

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
        
    nvs_stats_t stats;
    err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Used entries: %lu", stats.used_entries);
        ESP_LOGI(TAG, "Free entries: %lu", stats.free_entries);
        ESP_LOGI(TAG, "Available entries: %lu", stats.available_entries);
        ESP_LOGI(TAG, "Total entries: %lu", stats.total_entries);
    } else {
        ESP_LOGE(TAG, "Error getting NVS stats: %s\n", esp_err_to_name(err));
    }

    // Load all
    for (NvsConfigKey key = 0; key < NVS_CONFIG_COUNT; key++) {
        Settings *setting = &settings[key];

        nvs_config_init_fallback(key, setting);

        esp_err_t ret;

        int count = get_array_size(setting);
        setting->value = calloc(count, sizeof(ConfigValue));

        for (int idx = 0; idx < count; idx++) {
            char nvs_key[NVS_KEY_NAME_MAX_SIZE];
            get_nvs_key_name(setting, idx, nvs_key);

            switch (setting->type) {
                case TYPE_STR: {
                    size_t len = 0;
                    esp_err_t ret = nvs_get_str(handle, nvs_key, NULL, &len);
                    if (ret == ESP_OK && len > 1) {
                        char *buf = malloc(len);
                        if (buf) {
                            ret = nvs_get_str(handle, nvs_key, buf, &len);
                            if (ret == ESP_OK) {
                                setting->value[idx].str = buf;
                                break;
                            }
                            free(buf);
                        }
                    }

                    const char *def = setting->default_value.str ? setting->default_value.str : "";
                    setting->value[idx].str = strdup(def);
                    break;
                }
                case TYPE_U16: {
                    uint16_t val;
                    ret = nvs_get_u16(handle, nvs_key, &val);
                    setting->value[idx].u16 = (ret == ESP_OK) ? val : setting->default_value.u16;
                    break;
                }
                case TYPE_I32: {
                    int32_t val;
                    ret = nvs_get_i32(handle, nvs_key, &val);
                    setting->value[idx].i32 = (ret == ESP_OK) ? val : setting->default_value.i32;
                    break;
                }
                case TYPE_U64: {
                    uint64_t val;
                    ret = nvs_get_u64(handle, nvs_key, &val);
                    setting->value[idx].u64 = (ret == ESP_OK) ? val : setting->default_value.u64;
                    break;
                }
                case TYPE_FLOAT: {
                    char buf[32];
                    size_t len = sizeof(buf);
                    ret = nvs_get_str(handle, nvs_key, buf, &len);
                    if (ret == ESP_OK) {
                        char *end;
                        float parsed = strtof(buf, &end);
                        if (end != buf && *end == '\0') {
                            setting->value[idx].f = parsed;
                        } else {
                            ESP_LOGW(TAG, "Corrupt float in NVS for %s ('%s'), using default", setting->nvs_key_name, buf);
                            setting->value[idx].f = setting->default_value.f;
                        }
                    } else {
                        setting->value[idx].f = setting->default_value.f;
                    }
                    break;
                }
                case TYPE_BOOL: {
                    uint16_t val;
                    ret = nvs_get_u16(handle, nvs_key, &val);
                    setting->value[idx].b = (ret == ESP_OK) ? (val != 0) : setting->default_value.b;
                    break;
                }
            }
        }
    }

    nvs_save_queue = xQueueCreate(20, sizeof(ConfigUpdate));

    nvs_cache_mutex = xSemaphoreCreateMutex();
    if (!nvs_cache_mutex) {
        ESP_LOGE(TAG, "Failed to create nvs_cache_mutex");
        return ESP_FAIL;
    }

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
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return NULL;
    }
    if (setting->type != TYPE_STR || setting->array_size > 1) {
        ESP_LOGE(TAG, "Wrong type for %s (str)", setting->nvs_key_name);
        return NULL;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    char *result = strdup_psram(setting->value[0].str);
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

char *nvs_config_get_string_indexed(NvsConfigKey key, int index)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return NULL;
    }
    if (setting->type != TYPE_STR || setting->array_size < 1) {
        ESP_LOGE(TAG, "Wrong type for %s (indexed str)", setting->nvs_key_name);
        return NULL;
    }
    if (index < 0 || index >= setting->array_size) {
        ESP_LOGE(TAG, "Index out of bounds for key %s (%d)", setting->nvs_key_name, index);
        return NULL;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    char *result = strdup_psram(setting->value[index].str);
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

void nvs_config_set_string(NvsConfigKey key, const char *value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_STR || (setting->value[0].str && strcmp(setting->value[0].str, value) == 0)) return;

    ConfigUpdate update = { .key = key, .type = TYPE_STR, .value.str = strdup(value) };
    if (!update.value.str) return;
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

void nvs_config_set_string_indexed(NvsConfigKey key, int index, const char *value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_STR || setting->array_size < 1) return;
    if (index < 0 || index >= setting->array_size) return;
    if (setting->value[index].str && strcmp(setting->value[index].str, value) == 0) return;

    ConfigUpdate update = { .key = key, .type = TYPE_STR, .value.str = strdup(value), .index = index };
    if (!update.value.str) return;
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

uint16_t nvs_config_get_u16(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return 0;
    }
    if (setting->type != TYPE_U16) {
        ESP_LOGE(TAG, "Wrong type for %s (u16)", setting->nvs_key_name);
        return 0;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    uint16_t result = setting->value[0].u16;
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

void nvs_config_set_u16(NvsConfigKey key, uint16_t value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_U16 || setting->value[0].u16 == value) return;

    ConfigUpdate update = { .key = key, .type = TYPE_U16, .value.u16 = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

int32_t nvs_config_get_i32(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return 0;
    }
    if (setting->type != TYPE_I32) {
        ESP_LOGE(TAG, "Wrong type for %s (i32)", setting->nvs_key_name);
        return 0;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    int32_t result = setting->value[0].i32;
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

void nvs_config_set_i32(NvsConfigKey key, int32_t value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_I32 || setting->value[0].i32 == value) return;

    ConfigUpdate update = { .key = key, .type = TYPE_I32, .value.i32 = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

uint64_t nvs_config_get_u64(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return 0;
    }
    if (setting->type != TYPE_U64) {
        ESP_LOGE(TAG, "Wrong type for %s (u64)", setting->nvs_key_name);
        return 0;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    uint64_t result = setting->value[0].u64;
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

void nvs_config_set_u64(NvsConfigKey key, uint64_t value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_U64 || setting->value[0].u64 == value) return;

    ConfigUpdate update = { .key = key, .type = TYPE_U64, .value.u64 = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

float nvs_config_get_float(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return 0;
    }
    if (setting->type != TYPE_FLOAT) {
        ESP_LOGE(TAG, "Wrong type for %s (float)", setting->nvs_key_name);
        return 0;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    float result = setting->value[0].f;
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

void nvs_config_set_float(NvsConfigKey key, float value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_FLOAT || fabsf(setting->value[0].f - value) < 0.001f) return;

    ConfigUpdate update = { .key = key, .type = TYPE_FLOAT, .value.f = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}

bool nvs_config_get_bool(NvsConfigKey key)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting) {
        ESP_LOGE(TAG, "Invalid key %d", key);
        return false;
    }
    if (setting->type != TYPE_BOOL) {
        ESP_LOGE(TAG, "Wrong type for %s (bool)", setting->nvs_key_name);
        return false;
    }
    xSemaphoreTake(nvs_cache_mutex, portMAX_DELAY);
    bool result = setting->value[0].b;
    xSemaphoreGive(nvs_cache_mutex);
    return result;
}

void nvs_config_set_bool(NvsConfigKey key, bool value)
{
    Settings *setting = nvs_config_get_settings(key);
    if (!setting || setting->type != TYPE_BOOL || setting->value[0].b == value) return;

    ConfigUpdate update = { .key = key, .type = TYPE_BOOL, .value.b = value };
    xQueueSend(nvs_save_queue, &update, portMAX_DELAY);
}
