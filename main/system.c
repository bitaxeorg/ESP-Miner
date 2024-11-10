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

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "i2c_bitaxe.h"
#include "EMC2101.h"
#include "INA260.h"
#include "adc.h"
#include "connect.h"
#include "led_controller.h"
#include "nvs_config.h"
#include "display.h"
#include "vcore.h"


static const char * TAG = "SystemModule";

static void _suffix_string(uint64_t, char *, size_t, int);

static esp_netif_t * netif;

QueueHandle_t user_input_queue;

//local function prototypes
static esp_err_t ensure_overheat_mode_config();
static void _show_overheat_screen(GlobalState * GLOBAL_STATE);
static void _update_connection(GlobalState * GLOBAL_STATE);
static void _update_screen_one(GlobalState * GLOBAL_STATE);
static void _update_screen_two(GlobalState * GLOBAL_STATE);
static void show_ap_information(const char * error, GlobalState * GLOBAL_STATE);

static void _check_for_best_diff(GlobalState * GLOBAL_STATE, double diff, uint8_t job_id);
static void _suffix_string(uint64_t val, char * buf, size_t bufsiz, int sigdigits);

void SYSTEM_init_system(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->duration_start = 0;
    module->historical_hashrate_rolling_index = 0;
    module->historical_hashrate_init = 0;
    module->current_hashrate = 0;
    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0);
    module->best_session_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->FOUND_BLOCK = false;
    module->startup_done = false;
    
    // set the pool url
    module->pool_url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    module->fallback_pool_url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);

    //set the pool port
    module->pool_port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT);
    module->fallback_pool_port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT);

    // set fallback to false.
    module->is_using_fallback = false;

    // Initialize overheat_mode
    module->overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    // set the best diff string
    _suffix_string(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    _suffix_string(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);

    // set the ssid string to blank
    memset(module->ssid, 0, sizeof(module->ssid));

    // set the wifi_status to blank
    memset(module->wifi_status, 0, 20);
}


void SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE) {
    // Initialize the core voltage regulator
    VCORE_init(GLOBAL_STATE);
    VCORE_set_voltage(nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) / 1000.0, GLOBAL_STATE);

    //init the EMC2101, if we have one
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            EMC2101_init(nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, 1));
            break;
        case DEVICE_GAMMA:
            EMC2101_init(nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, 1));
            EMC2101_set_ideality_factor(EMC2101_IDEALITY_1_0319);
            EMC2101_set_beta_compensation(EMC2101_BETA_11);
            break;
        default:
    }

    //initialize the INA260, if we have one.
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version < 402) {
                // Initialize the LED controller
                INA260_init();
            }
            break;
        case DEVICE_GAMMA:
        default:
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    esp_err_t ret = ensure_overheat_mode_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to ensure overheat_mode config");
    }

    //Init the OLED
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            // oled
            if (display_init() != ESP_OK) {
                ESP_LOGW(TAG, "OLED init failed!");
            } else {
                ESP_LOGI(TAG, "OLED init success!");
                display_clear();
            }
            break;
        default:
    }

    netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    user_input_queue = xQueueCreate(10, sizeof(char[10])); // Create a queue to handle user input events
}

void SYSTEM_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    //_init_system(GLOBAL_STATE);

    char input_event[10];
    ESP_LOGI(TAG, "SYSTEM_task started");

    while (GLOBAL_STATE->ASIC_functions.init_fn == NULL) {
        show_ap_information("ASIC MODEL INVALID", GLOBAL_STATE);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    // show the connection screen
    while (!module->startup_done) {
        _update_connection(GLOBAL_STATE);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    if (display_active()) {
        display_show_logo();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    int current_screen = 0;
    TickType_t last_update_time = xTaskGetTickCount();

    while (1) {
        // Check for overheat mode
        if (module->overheat_mode == 1) {
            _show_overheat_screen(GLOBAL_STATE);
            vTaskDelay(5000 / portTICK_PERIOD_MS);  // Update every 5 seconds
            SYSTEM_update_overheat_mode(GLOBAL_STATE);  // Check for changes
            continue;  // Skip the normal screen cycle
        }

        // Update the current screen
        module->screen_page = current_screen;

        switch (current_screen) {
            case 0:
                _update_screen_one(GLOBAL_STATE);
                break;
            case 1:
                _update_screen_two(GLOBAL_STATE);
                break;
        }

        // Wait for user input or timeout
        bool input_received = false;
        TickType_t current_time = xTaskGetTickCount();
        TickType_t wait_time = pdMS_TO_TICKS(10000) - (current_time - last_update_time);

        if (wait_time > 0) {
            if (xQueueReceive(user_input_queue, &input_event, wait_time) == pdTRUE) {
                input_received = true;
                if (strcmp(input_event, "SHORT") == 0) {
                    ESP_LOGI(TAG, "Short button press detected, switching to next screen");
                    current_screen = (current_screen + 1) % 2;
                } else if (strcmp(input_event, "LONG") == 0) {
                    ESP_LOGI(TAG, "Long button press detected, toggling WiFi SoftAP");
                    toggle_wifi_softap();
                }
            }
        }

        // If no input received and 10 seconds have passed, switch to the next screen
        if (!input_received && (xTaskGetTickCount() - last_update_time) >= pdMS_TO_TICKS(10000)) {
            current_screen = (current_screen + 1) % 2;
        }

        last_update_time = xTaskGetTickCount();
    }
}



void SYSTEM_update_overheat_mode(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    uint16_t new_overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    
    if (new_overheat_mode != module->overheat_mode) {
        module->overheat_mode = new_overheat_mode;
        ESP_LOGI(TAG, "Overheat mode updated to: %d", module->overheat_mode);
    }
}

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_accepted++;
}
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_rejected++;
}

void SYSTEM_notify_mining_started(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->duration_start = esp_timer_get_time();
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

void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double found_diff, uint8_t job_id)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    // Calculate the time difference in seconds with sub-second precision
    // hashrate = (nonce_difficulty * 2^32) / time_to_find

    module->historical_hashrate[module->historical_hashrate_rolling_index] = GLOBAL_STATE->ASIC_difficulty;
    module->historical_hashrate_time_stamps[module->historical_hashrate_rolling_index] = esp_timer_get_time();

    module->historical_hashrate_rolling_index = (module->historical_hashrate_rolling_index + 1) % HISTORY_LENGTH;

    // ESP_LOGI(TAG, "nonce_diff %.1f, ttf %.1f, res %.1f", nonce_diff, duration,
    // historical_hashrate[historical_hashrate_rolling_index]);

    if (module->historical_hashrate_init < HISTORY_LENGTH) {
        module->historical_hashrate_init++;
    } else {
        module->duration_start =
            module->historical_hashrate_time_stamps[(module->historical_hashrate_rolling_index + 1) % HISTORY_LENGTH];
    }
    double sum = 0;
    for (int i = 0; i < module->historical_hashrate_init; i++) {
        sum += module->historical_hashrate[i];
    }

    double duration = (double) (esp_timer_get_time() - module->duration_start) / 1000000;

    double rolling_rate = (sum * 4294967296) / (duration * 1000000000);
    if (module->historical_hashrate_init < HISTORY_LENGTH) {
        module->current_hashrate = rolling_rate;
    } else {
        // More smoothing
        module->current_hashrate = ((module->current_hashrate * 9) + rolling_rate) / 10;
    }


    // logArrayContents(historical_hashrate, HISTORY_LENGTH);
    // logArrayContents(historical_hashrate_time_stamps, HISTORY_LENGTH);

    _check_for_best_diff(GLOBAL_STATE, found_diff, job_id);
}


/// 
/// LOCAL FUNCTIONS
/// 
static void _show_overheat_screen(GlobalState * GLOBAL_STATE)
{
    esp_netif_ip_info_t ip_info;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            if (display_active()) {
                esp_netif_get_ip_info(netif, &ip_info);
                char ip_address_str[IP4ADDR_STRLEN_MAX];
                esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);
                display_show_status((const char *[]){
                    "DEVICE OVERHEAT!", 
                    "See AxeOS settings", 
                    "IP:", 
                    ip_address_str
                }, 4);
            }
            break;
        default:
            break;
    }
}

static void _update_screen_one(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_netif_ip_info_t ip_info;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            if (display_active()) {
                esp_netif_get_ip_info(netif, &ip_info);
                char ip_address_str[IP4ADDR_STRLEN_MAX];
                esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);
                display_show_status((const char *[]){
                    "Mining URL:", 
                    module->is_using_fallback ? module->fallback_pool_url : module->pool_url, 
                    "Bitaxe IP:", 
                    ip_address_str
                }, 4);
            }
            break;
        default:
            break;
    }
}

static void _update_screen_two(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            if (display_active()) {
                char label_hashrate[20];
                snprintf(label_hashrate, 20, "Gh/s: %.2f", module->current_hashrate);

                char label_efficiency[20];
                float efficiency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power / (module->current_hashrate / 1000.0);
                snprintf(label_efficiency, 20, "J/Th: %.2f", efficiency);

                char label_best[20];
                snprintf(label_best, 27, "Best: %s/%s", module->best_session_diff_string, module->best_diff_string);

                char label_temp_avg[20];
                snprintf(label_temp_avg, 20, "Temp: %.1f C", power_management->chip_temp_avg);

                display_show_status((const char *[]){
                    label_hashrate, 
                    label_efficiency, 
                    module->FOUND_BLOCK ? "!!! BLOCK FOUND !!!" : label_best,
                    label_temp_avg
                }, 4);
            }
            break;
        default:
            break;
    }
}

static void _update_connection(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            if (display_active()) {

                char ap_ssid[13];
                generate_ssid(ap_ssid);

                display_show_status((const char *[]){
                    "Connecting to SSID:",
                    module->ssid,
                    "Configuration SSID:",
                    ap_ssid
                }, 4);
            }
            break;
        default:
    }
}


static void show_ap_information(const char * error, GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            if (display_active()) {
                char ap_ssid[13];
                generate_ssid(ap_ssid);

                display_show_status((const char *[]){
                    "Configuration SSID:",
                    ap_ssid
                }, 2);
            }
            break;
        default:
    }
}

static double _calculate_network_difficulty(uint32_t nBits)
{
    uint32_t mantissa = nBits & 0x007fffff;  // Extract the mantissa from nBits
    uint8_t exponent = (nBits >> 24) & 0xff; // Extract the exponent from nBits

    double target = (double) mantissa * pow(256, (exponent - 3)); // Calculate the target value

    double difficulty = (pow(2, 208) * 65535) / target; // Calculate the difficulty

    return difficulty;
}

static void _check_for_best_diff(GlobalState * GLOBAL_STATE, double diff, uint8_t job_id)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if ((uint64_t) diff > module->best_session_nonce_diff) {
        module->best_session_nonce_diff = (uint64_t) diff;
        _suffix_string((uint64_t) diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    if ((uint64_t) diff <= module->best_nonce_diff) {
        return;
    }
    module->best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, module->best_nonce_diff);

    // make the best_nonce_diff into a string
    _suffix_string((uint64_t) diff, module->best_diff_string, DIFF_STRING_SIZE, 0);

    double network_diff = _calculate_network_difficulty(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->target);
    if (diff > network_diff) {
        module->FOUND_BLOCK = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f > %f", diff, network_diff);
    }
    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void _suffix_string(uint64_t val, char * buf, size_t bufsiz, int sigdigits)
{
    const double dkilo = 1000.0;
    const uint64_t kilo = 1000ull;
    const uint64_t mega = 1000000ull;
    const uint64_t giga = 1000000000ull;
    const uint64_t tera = 1000000000000ull;
    const uint64_t peta = 1000000000000000ull;
    const uint64_t exa = 1000000000000000000ull;
    char suffix[2] = "";
    bool decimal = true;
    double dval;

    if (val >= exa) {
        val /= peta;
        dval = (double) val / dkilo;
        strcpy(suffix, "E");
    } else if (val >= peta) {
        val /= tera;
        dval = (double) val / dkilo;
        strcpy(suffix, "P");
    } else if (val >= tera) {
        val /= giga;
        dval = (double) val / dkilo;
        strcpy(suffix, "T");
    } else if (val >= giga) {
        val /= mega;
        dval = (double) val / dkilo;
        strcpy(suffix, "G");
    } else if (val >= mega) {
        val /= kilo;
        dval = (double) val / dkilo;
        strcpy(suffix, "M");
    } else if (val >= kilo) {
        dval = (double) val / dkilo;
        strcpy(suffix, "k");
    } else {
        dval = val;
        decimal = false;
    }

    if (!sigdigits) {
        if (decimal)
            snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
        else
            snprintf(buf, bufsiz, "%d%s", (unsigned int) dval, suffix);
    } else {
        /* Always show sigdigits + 1, padded on right with zeroes
         * followed by suffix */
        int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

        snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
    }
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
