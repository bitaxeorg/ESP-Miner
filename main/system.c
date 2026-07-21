#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "i2c_bitaxe.h"
#include "INA260.h"
#include "adc.h"
#include "connect.h"
#include "nvs_config.h"
#include "display.h"
#include "input.h"
#include "screen.h"
#include "vcore.h"
#include "thermal.h"
#include "utils.h"
#include "self_test.h"
#include "filesystem.h"
#include "work_queue.h"
#include "hashrate_monitor_task.h"

static const char * TAG = "system";

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

static void parse_pool_config_json(const char *json_str, PoolConfig *cfg, int index) {
    // Set default values first
    cfg->protocol = STRATUM_PROTOCOL_V1;
    cfg->url = strdup(index == 0 ? CONFIG_STRATUM_URL : "");
    cfg->port = index == 0 ? CONFIG_STRATUM_PORT : 3333;
    cfg->user = strdup(index == 0 ? CONFIG_STRATUM_USER : "");
    cfg->pass = strdup(index == 0 ? CONFIG_STRATUM_PW : "x");
    cfg->difficulty = index == 0 ? CONFIG_STRATUM_DIFFICULTY : 0;
#ifdef CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE
    cfg->extranonce_subscribe = true;
#else
    cfg->extranonce_subscribe = false;
#endif
    cfg->tls = index == 0 ? CONFIG_STRATUM_TLS : 0;
    cfg->cert = strdup("");
    cfg->decode_coinbase_tx = true;
    cfg->sv2_channel_type = SV2_CHANNEL_EXTENDED;
    cfg->sv2_authority_pubkey = strdup("");

    if (!json_str || strlen(json_str) == 0) {
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return;
    }

    cJSON *item;

    item = cJSON_GetObjectItem(root, "stratumProtocol");
    if (item && cJSON_IsString(item)) {
        stratum_protocol_t p = stratum_protocol_from_string(item->valuestring);
        if (p != STRATUM_PROTOCOL_UNKNOWN) cfg->protocol = p;
    }

    item = cJSON_GetObjectItem(root, "stratumURL");
    if (item && cJSON_IsString(item)) {
        free(cfg->url);
        cfg->url = strdup(item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "stratumPort");
    if (item && cJSON_IsNumber(item)) {
        cfg->port = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "stratumUser");
    if (item && cJSON_IsString(item)) {
        free(cfg->user);
        cfg->user = strdup(item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "stratumPassword");
    if (item && cJSON_IsString(item)) {
        free(cfg->pass);
        cfg->pass = strdup(item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "stratumSuggestedDifficulty");
    if (item && cJSON_IsNumber(item)) {
        cfg->difficulty = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "stratumExtranonceSubscribe");
    if (item && (cJSON_IsBool(item) || cJSON_IsNumber(item))) {
        cfg->extranonce_subscribe = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint != 0);
    }

    item = cJSON_GetObjectItem(root, "stratumTLS");
    if (item && cJSON_IsNumber(item)) {
        cfg->tls = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "stratumCert");
    if (item && cJSON_IsString(item)) {
        free(cfg->cert);
        cfg->cert = strdup(item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "stratumDecodeCoinbase");
    if (item && (cJSON_IsBool(item) || cJSON_IsNumber(item))) {
        cfg->decode_coinbase_tx = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint != 0);
    }

    item = cJSON_GetObjectItem(root, "stratumV2ChannelType");
    if (item && cJSON_IsString(item)) {
        sv2_channel_type_t t = sv2_channel_type_from_string(item->valuestring);
        if (t != SV2_CHANNEL_UNKNOWN) cfg->sv2_channel_type = t;
    }

    item = cJSON_GetObjectItem(root, "stratumV2AuthorityPubkey");
    if (item && cJSON_IsString(item)) {
        free(cfg->sv2_authority_pubkey);
        cfg->sv2_authority_pubkey = strdup(item->valuestring);
    }

    cJSON_Delete(root);
}

void SYSTEM_init_system(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF);
    module->best_session_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->block_found = 0;
    module->show_new_block = false;

    // Initialize network address strings
    strcpy(module->ip_addr_str, "");
    strcpy(module->ipv6_addr_str, "");
    strcpy(module->wifi_status, "Initializing...");
    
    // set the pool configurations
    for (int i = 0; i < MAX_POOLS; i++) {
        module->pools[i].url = NULL;
        module->pools[i].user = NULL;
        module->pools[i].pass = NULL;
        module->pools[i].cert = NULL;
        module->pools[i].sv2_authority_pubkey = NULL;
        SYSTEM_load_pool_from_nvs(GLOBAL_STATE, i);
    }

    // load primary and secondary pool index selectors
    module->primary_pool_index = nvs_config_get_u16(NVS_CONFIG_PRIMARY_POOL_INDEX);
    module->secondary_pool_index = nvs_config_get_u16(NVS_CONFIG_SECONDARY_POOL_INDEX);

    if (module->primary_pool_index >= MAX_POOLS) {
        module->primary_pool_index = 0;
    }
    if (module->secondary_pool_index >= MAX_POOLS) {
        module->secondary_pool_index = 1;
    }

    // use fallback stratum
    module->use_fallback_stratum = nvs_config_get_bool(NVS_CONFIG_USE_FALLBACK_STRATUM);

    // set based on config
    module->is_using_fallback = module->use_fallback_stratum;

    // Initialize pool connection info
    strcpy(module->pool_connection_info, "Not Connected");

    // Initialize overheat_mode
    module->overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    module->mining_paused = false;
    module->pools_unavailable = false;

    //Initialize power_fault fault mode
    module->power_fault = 0;

    // set the best diff string
    suffixString(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    suffixString(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);

    // Load stratum protocol selection from the active pool configuration
    uint16_t active_pool_idx = module->is_using_fallback ? module->secondary_pool_index : module->primary_pool_index;
    GLOBAL_STATE->stratum_protocol = module->pools[active_pool_idx].protocol;
    GLOBAL_STATE->sv2_conn = NULL;

    // Initialize mutexes
    pthread_mutex_init(&GLOBAL_STATE->valid_jobs_lock, NULL);
    GLOBAL_STATE->stratum_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
}

void SYSTEM_init_versions(GlobalState * GLOBAL_STATE) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    // Store the firmware version
    GLOBAL_STATE->SYSTEM_MODULE.version = strdup(app_desc->version);
    if (GLOBAL_STATE->SYSTEM_MODULE.version == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for version");
        GLOBAL_STATE->SYSTEM_MODULE.version = strdup("Unknown");
    }
    
    // Read AxeOS version from SPIFFS
    FILE *f = fopen("/version.txt", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to open /version.txt");
        GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup("Unknown");
    } else {
        char version[64];
        if (fgets(version, sizeof(version), f) == NULL) {
            ESP_LOGW(TAG, "Failed to read version from /version.txt");
            GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup("Unknown");
        } else {
            // Remove trailing newline if present
            size_t len = strlen(version);
            if (len > 0 && version[len - 1] == '\n') {
                version[len - 1] = '\0';
            }
            GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup(version);
            if (GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for axeOSVersion");
                GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup("Unknown");
            }
        }
        fclose(f);
    }
    
    ESP_LOGI(TAG, "Firmware Version: %s", GLOBAL_STATE->SYSTEM_MODULE.version);
    ESP_LOGI(TAG, "AxeOS Version: %s", GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion);

    if (strcmp(GLOBAL_STATE->SYSTEM_MODULE.version, GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion) != 0) {
        ESP_LOGE(TAG, "Firmware (%s) and AxeOS (%s) versions do not match. Please make sure to update both www.bin and esp-miner.bin.", 
            GLOBAL_STATE->SYSTEM_MODULE.version, 
            GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion);
    }
}

esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE) {
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "ISR:FAIL");
        ESP_LOGE(TAG, "Error installing ISR service");
        return ret;
    }

    ret = display_init(GLOBAL_STATE);
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "DISPLAY:FAIL");
        ESP_LOGE(TAG, "Display init failed");
        return ret;
    }

    if (!GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        ret = input_init(screen_button_press, toggle_wifi_softap);
    } else {
        ret = input_init(NULL, self_test_reset);
    }
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "INPUT:FAIL");
        ESP_LOGE(TAG, "Input init failed");
        return ret;
    }

    ret = screen_start(GLOBAL_STATE);
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "SCREEN:FAIL");
        ESP_LOGE(TAG, "Screen start failed");
        return ret;
    }

    ret = ensure_overheat_mode_config();
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "CONFIG:FAIL");
        ESP_LOGE(TAG, "Failed to ensure overheat_mode config");
        return ret;
    }

    ret = filesystem_init(GLOBAL_STATE);
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "FILESYS:FAIL");
        ESP_LOGE(TAG, "Filesystem init failed");
        if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
            return ret;
        }
    }

    // Initialize the core voltage regulator
    ret = VCORE_init(GLOBAL_STATE);
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "VCORE:FAIL");
        ESP_LOGE(TAG, "VCORE init failed");
        return ret;
    }

    // For self-test, we set a stable known voltage before ASIC initialization
    if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        vTaskDelay(500 / portTICK_PERIOD_MS);

        ret = VCORE_set_voltage(GLOBAL_STATE, (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv / 1000.0f);
        if (ret != ESP_OK) {
            self_test_show_message(GLOBAL_STATE, "VCORE:FAIL");
            ESP_LOGE(TAG, "VCORE set failed");
            return ret;
        }
    }

    ret = Thermal_init(&GLOBAL_STATE->DEVICE_CONFIG);
    if (ret != ESP_OK) {
        self_test_show_message(GLOBAL_STATE, "THERMAL:FAIL");
        ESP_LOGE(TAG, "Thermal init failed");
        return ret;
    }

    return ESP_OK;
}

void SYSTEM_clean_jobs_queue(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Clean Jobs: clearing queue");
    queue_clear(&GLOBAL_STATE->stratum_queue);

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    for (int i = 0; i < 128; i = i + 4) {
        GLOBAL_STATE->valid_jobs[i] = 0;
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    // Reset hashrate measurements to prevent a spike on reconnection
    hashrate_monitor_reset_measurements(GLOBAL_STATE);
}

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_accepted++;
}

static int compare_rejected_reason_stats(const void *a, const void *b) {
    const RejectedReasonStat *ea = a;
    const RejectedReasonStat *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_rejected++;

    for (int i = 0; i < module->rejected_reason_stats_count; i++) {
        if (strncmp(module->rejected_reason_stats[i].message, error_msg, sizeof(module->rejected_reason_stats[i].message) - 1) == 0) {
            module->rejected_reason_stats[i].count++;
            return;
        }
    }

    if (module->rejected_reason_stats_count < (int)(sizeof(module->rejected_reason_stats) / sizeof(module->rejected_reason_stats[0]))) {
        strncpy(module->rejected_reason_stats[module->rejected_reason_stats_count].message, 
                error_msg, 
                sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1);
        module->rejected_reason_stats[module->rejected_reason_stats_count].message[sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1] = '\0'; // Ensure null termination
        module->rejected_reason_stats[module->rejected_reason_stats_count].count = 1;
        module->rejected_reason_stats_count++;
    }

    if (module->rejected_reason_stats_count > 1) {
        qsort(module->rejected_reason_stats, module->rejected_reason_stats_count, 
            sizeof(module->rejected_reason_stats[0]), compare_rejected_reason_stats);
    }    
}

void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    // Hourly clock sync
    if (module->lastClockSync + (60 * 60) > ntime) {
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    module->lastClockSync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, uint32_t target)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if ((uint64_t) diff > module->best_session_nonce_diff) {
        module->best_session_nonce_diff = (uint64_t) diff;
        suffixString((uint64_t) diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    double network_diff = networkDifficulty(target);
    if (diff >= network_diff) {
        module->block_found++;
        module->show_new_block = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f >= %f (count: %d)", diff, network_diff, module->block_found);
    }

    if ((uint64_t) diff <= module->best_nonce_diff) {
        return;
    }
    module->best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, module->best_nonce_diff);

    // make the best_nonce_diff into a string
    suffixString((uint64_t) diff, module->best_diff_string, DIFF_STRING_SIZE, 0);

    ESP_LOGI(TAG, "New best difficulty: %s", module->best_diff_string);
}

static esp_err_t ensure_overheat_mode_config() {
    bool overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);

    ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);

    return ESP_OK;
}

stratum_protocol_t stratum_protocol_from_string(const char *s)
{
    if (!s) return STRATUM_PROTOCOL_UNKNOWN;
    if (strcmp(s, STRATUM_V1) == 0) return STRATUM_PROTOCOL_V1;
    if (strcmp(s, STRATUM_V2) == 0) return STRATUM_PROTOCOL_V2;
    return STRATUM_PROTOCOL_UNKNOWN;
}

sv2_channel_type_t sv2_channel_type_from_string(const char *s)
{
    if (!s) return SV2_CHANNEL_UNKNOWN;
    if (strcmp(s, SV2_CHANNEL_TYPE_EXTENDED) == 0) return SV2_CHANNEL_EXTENDED;
    if (strcmp(s, SV2_CHANNEL_TYPE_STANDARD) == 0) return SV2_CHANNEL_STANDARD;
    return SV2_CHANNEL_UNKNOWN;
}

void SYSTEM_load_pool_from_nvs(GlobalState * GLOBAL_STATE, int i) {
    if (i < 0 || i >= MAX_POOLS) return;
    
    PoolConfig *cfg = &GLOBAL_STATE->SYSTEM_MODULE.pools[i];
    free(cfg->url);
    free(cfg->user);
    free(cfg->pass);
    free(cfg->cert);
    free(cfg->sv2_authority_pubkey);
    
    cfg->url = NULL;
    cfg->user = NULL;
    cfg->pass = NULL;
    cfg->cert = NULL;
    cfg->sv2_authority_pubkey = NULL;

    char *json_str = nvs_config_get_string_indexed(NVS_CONFIG_POOL, i);
    parse_pool_config_json(json_str, cfg, i);
    free(json_str);
}
