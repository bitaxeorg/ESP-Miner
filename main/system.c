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
#include "asic_task_module.h"
#include "system_module.h"
#include "device_config.h"
#include "pool_module.h"
#include "state_module.h"


static const char * TAG = "system";

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

static void _check_for_best_diff( double diff, uint8_t job_id);

typedef struct
{
    // The starting time for a certain period of mining activity.
    double duration_start;

    // The index to keep track of the rolling historical hashrate data.
    int historical_hashrate_rolling_index;

    // An array to store timestamps corresponding to historical hashrate values.
    double historical_hashrate_time_stamps[HISTORY_LENGTH];

    // An array to store historical hashrate values over a defined period.
    double historical_hashrate[HISTORY_LENGTH];

    // A flag indicating if the historical hashrate data is initialized.
    int historical_hashrate_init;

    
} HashHistory;
HashHistory HASH_HISTORY;

// Timestamp of the last clock synchronization event.
uint32_t lastClockSync;

// The difficulty (nonce) for the best solution found during mining.
uint64_t best_nonce_diff;
void SYSTEM_init_system()
{

    HASH_HISTORY.duration_start = 0;
    HASH_HISTORY.historical_hashrate_rolling_index = 0;
    HASH_HISTORY.historical_hashrate_init = 0;
    SYSTEM_MODULE.current_hashrate = 0;
    SYSTEM_MODULE.shares_accepted = 0;
    SYSTEM_MODULE.shares_rejected = 0;
    SYSTEM_MODULE.best_session_nonce_diff = 0;
    best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0);
    SYSTEM_MODULE.start_time = esp_timer_get_time();
    lastClockSync = 0;
    STATE_MODULE.FOUND_BLOCK = false;
    
    // set the pool url
    POOL_MODULE.pools[0].url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    POOL_MODULE.pools[1].url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);

    // set the pool port
    POOL_MODULE.pools[0].port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT);
    POOL_MODULE.pools[1].port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT);

    // set the pool user
    POOL_MODULE.pools[0].user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    POOL_MODULE.pools[1].user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);

    // set the pool password
    POOL_MODULE.pools[0].pass = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS, CONFIG_STRATUM_PW);
    POOL_MODULE.pools[1].pass = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, CONFIG_FALLBACK_STRATUM_PW);

    // set the pool difficulty
    POOL_MODULE.pools[0].difficulty = nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY, CONFIG_STRATUM_DIFFICULTY);
    POOL_MODULE.pools[1].difficulty =
        nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY, CONFIG_FALLBACK_STRATUM_DIFFICULTY);

    // set the pool extranonce subscribe
    POOL_MODULE.pools[0].extranonce_subscribe =
        nvs_config_get_u16(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE, STRATUM_EXTRANONCE_SUBSCRIBE);
    POOL_MODULE.pools[1].extranonce_subscribe =
        nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE, FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);

    // set fallback to false.
    POOL_MODULE.default_pool_idx = 0;

    // Initialize overheat_mode
    STATE_MODULE.overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", STATE_MODULE.overheat_mode);

    //Initialize power_fault fault mode
    STATE_MODULE.power_fault = 0;

    // set the best diff string
    suffixString(best_nonce_diff, SYSTEM_MODULE.best_diff_string, DIFF_STRING_SIZE, 0);
    suffixString(SYSTEM_MODULE.best_session_nonce_diff, SYSTEM_MODULE.best_session_diff_string, DIFF_STRING_SIZE, 0);
}

esp_err_t SYSTEM_init_peripherals() {
    
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(), TAG, "VCORE init failed!");
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) / 1000.0), TAG, "VCORE set voltage failed!");

    ESP_RETURN_ON_ERROR(Thermal_init(DEVICE_CONFIG), TAG, "Thermal init failed!");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    ESP_RETURN_ON_ERROR(display_init(), TAG, "Display init failed!");

    ESP_RETURN_ON_ERROR(input_init(screen_next, toggle_wifi_softap), TAG, "Input init failed!");

    ESP_RETURN_ON_ERROR(screen_start(), TAG, "Screen start failed!");

    return ESP_OK;
}

void SYSTEM_notify_accepted_share()
{
    SYSTEM_MODULE.shares_accepted++;
}

static int compare_rejected_reason_stats(const void *a, const void *b) {
    const RejectedReasonStat *ea = a;
    const RejectedReasonStat *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

void SYSTEM_notify_rejected_share(char * error_msg)
{

    SYSTEM_MODULE.shares_rejected++;

    for (int i = 0; i < SYSTEM_MODULE.rejected_reason_stats_count; i++) {
        if (strncmp(SYSTEM_MODULE.rejected_reason_stats[i].message, error_msg, sizeof(SYSTEM_MODULE.rejected_reason_stats[i].message) - 1) == 0) {
            SYSTEM_MODULE.rejected_reason_stats[i].count++;
            return;
        }
    }

    if (SYSTEM_MODULE.rejected_reason_stats_count < sizeof(SYSTEM_MODULE.rejected_reason_stats)) {
        strncpy(SYSTEM_MODULE.rejected_reason_stats[SYSTEM_MODULE.rejected_reason_stats_count].message, 
                error_msg, 
                sizeof(SYSTEM_MODULE.rejected_reason_stats[SYSTEM_MODULE.rejected_reason_stats_count].message) - 1);
        SYSTEM_MODULE.rejected_reason_stats[SYSTEM_MODULE.rejected_reason_stats_count].message[sizeof(SYSTEM_MODULE.rejected_reason_stats[SYSTEM_MODULE.rejected_reason_stats_count].message) - 1] = '\0'; // Ensure null termination
        SYSTEM_MODULE.rejected_reason_stats[SYSTEM_MODULE.rejected_reason_stats_count].count = 1;
        SYSTEM_MODULE.rejected_reason_stats_count++;
    }

    if (SYSTEM_MODULE.rejected_reason_stats_count > 1) {
        qsort(SYSTEM_MODULE.rejected_reason_stats, SYSTEM_MODULE.rejected_reason_stats_count, 
            sizeof(SYSTEM_MODULE.rejected_reason_stats[0]), compare_rejected_reason_stats);
    }    
}

void SYSTEM_notify_mining_started()
{
    HASH_HISTORY.duration_start = esp_timer_get_time();
}

void SYSTEM_notify_new_ntime( uint32_t ntime)
{
    // Hourly clock sync
    if (lastClockSync + (60 * 60) > ntime) {
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    lastClockSync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void SYSTEM_notify_found_nonce(double found_diff, uint8_t job_id)
{
    // Calculate the time difference in seconds with sub-second precision
    // hashrate = (nonce_difficulty * 2^32) / time_to_find

    HASH_HISTORY.historical_hashrate[HASH_HISTORY.historical_hashrate_rolling_index] = DEVICE_CONFIG.family.asic.difficulty;
    HASH_HISTORY.historical_hashrate_time_stamps[HASH_HISTORY.historical_hashrate_rolling_index] = esp_timer_get_time();

    HASH_HISTORY.historical_hashrate_rolling_index = (HASH_HISTORY.historical_hashrate_rolling_index + 1) % HISTORY_LENGTH;

    // ESP_LOGI(TAG, "nonce_diff %.1f, ttf %.1f, res %.1f", nonce_diff, duration,
    // historical_hashrate[historical_hashrate_rolling_index]);

    if (HASH_HISTORY.historical_hashrate_init < HISTORY_LENGTH) {
        HASH_HISTORY.historical_hashrate_init++;
    } else {
        HASH_HISTORY.duration_start =
            HASH_HISTORY.historical_hashrate_time_stamps[(HASH_HISTORY.historical_hashrate_rolling_index + 1) % HISTORY_LENGTH];
    }
    double sum = 0;
    for (int i = 0; i < HASH_HISTORY.historical_hashrate_init; i++) {
        sum += HASH_HISTORY.historical_hashrate[i];
    }

    double duration = (double) (esp_timer_get_time() - HASH_HISTORY.duration_start) / 1000000;

    double rolling_rate = (sum * 4294967296) / (duration * 1000000000);
    if (HASH_HISTORY.historical_hashrate_init < HISTORY_LENGTH) {
        SYSTEM_MODULE.current_hashrate = rolling_rate;
    } else {
        // More smoothing
        SYSTEM_MODULE.current_hashrate = ((SYSTEM_MODULE.current_hashrate * 9) + rolling_rate) / 10;
    }

    
    // logArrayContents(historical_hashrate, HISTORY_LENGTH);
    // logArrayContents(historical_hashrate_time_stamps, HISTORY_LENGTH);

    _check_for_best_diff(found_diff, job_id);
}

static void _check_for_best_diff( double diff, uint8_t job_id)
{
    if ((uint64_t) diff > SYSTEM_MODULE.best_session_nonce_diff) {
        SYSTEM_MODULE.best_session_nonce_diff = (uint64_t) diff;
        suffixString((uint64_t) diff, SYSTEM_MODULE.best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    double network_diff = networkDifficulty(ASIC_TASK_MODULE.active_jobs[job_id]->target);
    if (diff > network_diff) {
        STATE_MODULE.FOUND_BLOCK = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f > %f", diff, network_diff);
    }

    if ((uint64_t) diff <= best_nonce_diff) {
        return;
    }
    best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, best_nonce_diff);

    // make the best_nonce_diff into a string
    suffixString((uint64_t) diff, SYSTEM_MODULE.best_diff_string, DIFF_STRING_SIZE, 0);

    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

static esp_err_t ensure_overheat_mode_config() {
    uint16_t overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, UINT16_MAX);

    if (overheat_mode == UINT16_MAX) {
        // Key doesn't exist or couldn't be read, set the default value
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        ESP_LOGI(TAG, "Default value for overheat_mode set to 0");
    } else {
        // Key exists, log the current value
        ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);
    }

    return ESP_OK;
}
