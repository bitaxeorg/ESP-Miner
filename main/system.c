#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

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

static const char * TAG = "system";

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

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
    module->block_found = false;
    
    // Initialize network address strings
    strcpy(module->ip_addr_str, "");
    strcpy(module->ipv6_addr_str, "");
    strcpy(module->wifi_status, "Initializing...");
    
    // set the pool url
    module->pool_url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    module->fallback_pool_url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);

    // set the pool port
    module->pool_port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT);
    module->fallback_pool_port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT);

    // set the pool tls
    module->pool_tls = nvs_config_get_u16(NVS_CONFIG_STRATUM_TLS);
    module->fallback_pool_tls = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_TLS);

    // set the pool cert
    module->pool_cert = nvs_config_get_string(NVS_CONFIG_STRATUM_CERT);
    module->fallback_pool_cert = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_CERT);

    // set the pool user
    module->pool_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    module->fallback_pool_user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);

    // set the pool password
    module->pool_pass = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS);
    module->fallback_pool_pass = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS);

    // set the pool difficulty
    module->pool_difficulty = nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY);
    module->fallback_pool_difficulty = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY);

    // set the pool extranonce subscribe
    module->pool_extranonce_subscribe = nvs_config_get_bool(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE);
    module->fallback_pool_extranonce_subscribe = nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);

    // use fallback stratum
    module->use_fallback_stratum = nvs_config_get_bool(NVS_CONFIG_USE_FALLBACK_STRATUM);

    // set based on config
    module->is_using_fallback = module->use_fallback_stratum;

    // Initialize pool connection info
    strcpy(module->pool_connection_info, "Not Connected");

    // Initialize overheat_mode
    module->overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    //Initialize power_fault fault mode
    module->power_fault = 0;

    // set the best diff string
    suffixString(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    suffixString(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);

    // Initialize mutexes
    pthread_mutex_init(&GLOBAL_STATE->valid_jobs_lock, NULL);
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
    
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(GLOBAL_STATE), TAG, "VCORE init failed!");

    ESP_RETURN_ON_ERROR(Thermal_init(&GLOBAL_STATE->DEVICE_CONFIG), TAG, "Thermal init failed!");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    ESP_RETURN_ON_ERROR(display_init(GLOBAL_STATE), TAG, "Display init failed!");

    ESP_RETURN_ON_ERROR(input_init(screen_button_press, toggle_wifi_softap), TAG, "Input init failed!");

    ESP_RETURN_ON_ERROR(screen_start(GLOBAL_STATE), TAG, "Screen start failed!");

    return ESP_OK;
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

    if (module->rejected_reason_stats_count < sizeof(module->rejected_reason_stats)) {
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

void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, uint8_t job_id)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if ((uint64_t) diff > module->best_session_nonce_diff) {
        module->best_session_nonce_diff = (uint64_t) diff;
        suffixString((uint64_t) diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    double network_diff = networkDifficulty(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->target);
    if (diff >= network_diff) {
        module->block_found = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f >= %f", diff, network_diff);
    }

    if ((uint64_t) diff <= module->best_nonce_diff) {
        return;
    }
    module->best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, module->best_nonce_diff);

    // make the best_nonce_diff into a string
    suffixString((uint64_t) diff, module->best_diff_string, DIFF_STRING_SIZE, 0);

    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

static esp_err_t ensure_overheat_mode_config() {
    bool overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);

    ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);

    return ESP_OK;
}
