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
#include "esp_attr.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "global_state.h"
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

#define NVS_COUNTER_UPDATE_INTERVAL_MS 60 * 60 * 1000  // Update NVS once per hour
#define NOINIT_SENTINEL_VALUE 0x4C4F4732       // "LOG2" in hex

typedef struct
{
    uint64_t total_uptime;            // Total uptime in seconds
    uint64_t cumulative_hashes_high;  // High 64 bits of 128-bit cumulative hash count
    uint64_t cumulative_hashes_low;   // Low 64 bits of 128-bit cumulative hash count
    uint32_t sentinel;                // Magic value to detect valid noinit data
} NoinitState;    

__NOINIT_ATTR static NoinitState noinit_state; // Noinit state survives soft reboots but is lost on power cycle
static uint64_t last_update_time_ms;
static uint64_t last_nvs_write_time_ms;
static uint64_t total_uptime_at_system_start;

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
    module->start_time_us = esp_timer_get_time();
    module->lastClockSync = 0;
    module->block_found = 0;
    module->show_new_block = false;

    if (noinit_state.sentinel != NOINIT_SENTINEL_VALUE) {
        noinit_state.sentinel = NOINIT_SENTINEL_VALUE;
        noinit_state.total_uptime = 0;
        noinit_state.cumulative_hashes_high = 0;
        noinit_state.cumulative_hashes_low = 0;
    }

    // Load values from NVS (persist across power cycle)
    uint64_t nvs_total_uptime = nvs_config_get_u64(NVS_CONFIG_TOTAL_UPTIME);
    if (nvs_total_uptime > noinit_state.total_uptime) {
        noinit_state.total_uptime = nvs_total_uptime;
    }
    total_uptime_at_system_start = noinit_state.total_uptime;

    uint64_t nvs_hashes_high = nvs_config_get_u64(NVS_CONFIG_CUMULATIVE_HASHES_HIGH);
    uint64_t nvs_hashes_low = nvs_config_get_u64(NVS_CONFIG_CUMULATIVE_HASHES_LOW);
    if (nvs_hashes_high > noinit_state.cumulative_hashes_high) {
        noinit_state.cumulative_hashes_high = nvs_hashes_high;
        noinit_state.cumulative_hashes_low = nvs_hashes_low;
    } else if (nvs_hashes_high == noinit_state.cumulative_hashes_high &&
               nvs_hashes_low > noinit_state.cumulative_hashes_low) {
        noinit_state.cumulative_hashes_low = nvs_hashes_low;
    }

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

    // set the pool decode coinbase
    module->pool_decode_coinbase = nvs_config_get_bool(NVS_CONFIG_STRATUM_DECODE_COINBASE);
    module->fallback_pool_decode_coinbase = nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE);

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

void SYSTEM_init_versions(GlobalState * GLOBAL_STATE)
{
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

    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

static esp_err_t ensure_overheat_mode_config() {
    bool overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);

    ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);

    return ESP_OK;
}

void SYSTEM_noinit_update(SystemModule * SYSTEM_MODULE)
{
    uint64_t current_time_ms = esp_timer_get_time() / 1000;
    
    // Initialize last_update_time on first call
    if (last_update_time_ms == 0) {
        last_update_time_ms = current_time_ms;
        last_nvs_write_time_ms = current_time_ms;
        return;
    }

    uint64_t elapsed_ms = current_time_ms - last_update_time_ms;
    last_update_time_ms = current_time_ms;
    
    // Only update if at least 1 second has passed
    if (elapsed_ms < 1000) {
        return;
    }
    
    SYSTEM_MODULE->uptime_seconds = (esp_timer_get_time() - SYSTEM_MODULE->start_time_us) / 1000000;
    noinit_state.total_uptime = total_uptime_at_system_start + SYSTEM_MODULE->uptime_seconds;
    
    // Update cumulative hashes: hashrate (GH/s) × milliseconds × 1e6 = raw hashes
    uint64_t hashes_done = elapsed_ms * 1e6 * SYSTEM_MODULE->current_hashrate;
    uint64_t new_low = noinit_state.cumulative_hashes_low + hashes_done;
    if (new_low < noinit_state.cumulative_hashes_low) {
        noinit_state.cumulative_hashes_high++;
    }
    noinit_state.cumulative_hashes_low = new_low;

    // Persist to NVS once per hour to reduce wear
    if (current_time_ms - last_nvs_write_time_ms >= NVS_COUNTER_UPDATE_INTERVAL_MS) {
        nvs_config_set_u64(NVS_CONFIG_TOTAL_UPTIME, noinit_state.total_uptime);
        nvs_config_set_u64(NVS_CONFIG_CUMULATIVE_HASHES_HIGH, noinit_state.cumulative_hashes_high);
        nvs_config_set_u64(NVS_CONFIG_CUMULATIVE_HASHES_LOW, noinit_state.cumulative_hashes_low);
        last_nvs_write_time_ms = current_time_ms;
    }
}

uint64_t SYSTEM_noinit_get_total_uptime_seconds()
{
    return noinit_state.total_uptime;
}

// Convert 128-bit to double: high * 2^64 + low. Loses precision for very large values, but sufficient for display
double SYSTEM_noinit_get_total_hashes()
{
    return (double)noinit_state.cumulative_hashes_high * 18446744073709551616.0 + (double)noinit_state.cumulative_hashes_low;
}

double SYSTEM_noinit_get_total_log2_work()
{
    // If high part is 0, just compute log2 of low part
    if (noinit_state.cumulative_hashes_high == 0) {
        if (noinit_state.cumulative_hashes_low == 0) {
            return 0.0;
        }
        return log2((double)noinit_state.cumulative_hashes_low);
    }
    
    // For 128-bit value: log2(high * 2^64 + low) = 64 + log2(high + low/2^64)
    // Since low/2^64 is very small compared to high, we approximate:
    // log2(high * 2^64 + low) ≈ 64 + log2(high) for large values
    // More precise: 64 + log2(high + low/2^64)
    double high_plus_fraction = (double)noinit_state.cumulative_hashes_high + 
                                (double)noinit_state.cumulative_hashes_low / 18446744073709551616.0;
    return 64.0 + log2(high_plus_fraction);
}
