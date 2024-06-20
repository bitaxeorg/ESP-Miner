#include "system.h"

#include "esp_log.h"

#include "i2c_master.h"
#include "EMC2101.h"
#include "INA260.h"
#include "TMP1075.h"
#include "adc.h"
#include "connect.h"
#include "led_controller.h"
#include "nvs_config.h"
#include "oled.h"
#include "vcore.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

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

static const char * TAG = "SystemModule";

static void _suffix_string(uint64_t, char *, size_t, int);

static esp_netif_t * netif;
static esp_netif_ip_info_t ip_info;

QueueHandle_t user_input_queue;

static void _init_system(GlobalState * GLOBAL_STATE)
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

    //set the pool port
    module->pool_port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT);

    // set the best diff string
    _suffix_string(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    _suffix_string(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);

    // set the ssid string to blank
    memset(module->ssid, 0, sizeof(module->ssid));

    // set the wifi_status to blank
    memset(module->wifi_status, 0, 20);

    // test the LEDs
    //  ESP_LOGI(TAG, "Init LEDs!");
    //  ledc_init();
    //  led_set();

    // Init I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    // Initialize the core voltage regulator
    VCORE_init(GLOBAL_STATE);
    VCORE_set_voltage(nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) / 1000.0, GLOBAL_STATE);

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            EMC2101_init(nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, 1));
            break;
        default:
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            // oled
            if (!OLED_init()) {
                ESP_LOGI(TAG, "OLED init failed!");
            } else {
                ESP_LOGI(TAG, "OLED init success!");
                // clear the oled screen
                OLED_fill(0);
            }
            break;
        default:
    }

    netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
}

static void _update_hashrate(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if (module->screen_page != 0) {
        return;
    }

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            float efficiency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power / (module->current_hashrate / 1000.0);
            OLED_clearLine(0);
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, "Gh%s: %.1f J/Th: %.1f", module->historical_hashrate_init < HISTORY_LENGTH ? "*" : "",
                    module->current_hashrate, efficiency);
            OLED_writeString(0, 0, module->oled_buf);
            break;
        default:
    }
}

static void _update_shares(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if (module->screen_page != 0) {
        return;
    }
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            OLED_clearLine(1);
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, "A/R: %u/%u", module->shares_accepted, module->shares_rejected);
            OLED_writeString(0, 1, module->oled_buf);
            break;
        default:
    }
}

static void _update_best_diff(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if (module->screen_page != 0) {
        return;
    }

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            OLED_clearLine(3);
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, module->FOUND_BLOCK ? "!!! BLOCK FOUND !!!" : "BD: %s", module->best_diff_string);
            OLED_writeString(0, 3, module->oled_buf);
            break;
        default:
    }
}

static void _clear_display(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            OLED_clearLine(0);
            OLED_clearLine(1);
            OLED_clearLine(2);
            OLED_clearLine(3);
            break;
        default:
    }
}

static void _update_system_info(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (OLED_status()) {

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, " Fan: %d RPM", power_management->fan_rpm);
                OLED_writeString(0, 0, module->oled_buf);

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "Temp: %.1f C", power_management->chip_temp_avg);
                OLED_writeString(0, 1, module->oled_buf);

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, " Pwr: %.3f W", power_management->power);
                OLED_writeString(0, 2, module->oled_buf);

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, " %i mV: %i mA", (int) power_management->voltage, (int) power_management->current);
                OLED_writeString(0, 3, module->oled_buf);
            }
            break;
        default:
    }
}

static void _update_esp32_info(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    uint32_t free_heap_size = esp_get_free_heap_size();

    uint16_t vcore = VCORE_get_voltage_mv(GLOBAL_STATE);

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (OLED_status()) {

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "FH: %lu bytes", free_heap_size);
                OLED_writeString(0, 0, module->oled_buf);

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "vCore: %u mV", vcore);
                OLED_writeString(0, 1, module->oled_buf);

                esp_netif_get_ip_info(netif, &ip_info);
                char ip_address_str[IP4ADDR_STRLEN_MAX];
                esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "IP: %s", ip_address_str);
                OLED_writeString(0, 2, module->oled_buf);

                OLED_writeString(0, 3, esp_app_get_description()->version);
            }
            break;
        default:
    }
}

static void _init_connection(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (OLED_status()) {
                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "Connecting to SSID:");
                OLED_writeString(0, 0, module->oled_buf);
            }
            break;
        default:
    }
}

static void _update_connection(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (OLED_status()) {
                OLED_clearLine(2);
                strncpy(module->oled_buf, module->ssid, sizeof(module->oled_buf));
                module->oled_buf[sizeof(module->oled_buf) - 1] = 0;
                OLED_writeString(0, 1, module->oled_buf);
                
                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "Configuration SSID:");
                OLED_writeString(0, 2, module->oled_buf);

                char ap_ssid[13];
                generate_ssid(ap_ssid);
                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, ap_ssid);
                OLED_writeString(0, 3, module->oled_buf);
            }
            break;
        default:
    }
}

static void _update_system_performance(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    // Calculate the uptime in seconds
    double uptime_in_seconds = (esp_timer_get_time() - module->start_time) / 1000000;
    int uptime_in_days = uptime_in_seconds / (3600 * 24);
    int remaining_seconds = (int) uptime_in_seconds % (3600 * 24);
    int uptime_in_hours = remaining_seconds / 3600;
    remaining_seconds %= 3600;
    int uptime_in_minutes = remaining_seconds / 60;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (OLED_status()) {

                _update_hashrate(GLOBAL_STATE);
                _update_shares(GLOBAL_STATE);
                _update_best_diff(GLOBAL_STATE);

                memset(module->oled_buf, 0, 20);
                snprintf(module->oled_buf, 20, "UT: %dd %ih %im", uptime_in_days, uptime_in_hours, uptime_in_minutes);
                OLED_writeString(0, 2, module->oled_buf);
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
            if (OLED_status()) {
                _clear_display(GLOBAL_STATE);
                if (error != NULL) {
                    OLED_writeString(0, 0, error);
                }
                OLED_writeString(0, 1, "Configuration SSID:");
                char ap_ssid[13];
                generate_ssid(ap_ssid);
                OLED_writeString(0, 2, ap_ssid);
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

void SYSTEM_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    _init_system(GLOBAL_STATE);
    user_input_queue = xQueueCreate(10, sizeof(char[10])); // Create a queue to handle user input events

    _clear_display(GLOBAL_STATE);
    _init_connection(GLOBAL_STATE);

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

    while (1) {
        // Automatically cycle through screens
        for (int screen = 0; screen < 3; screen++) {
            _clear_display(GLOBAL_STATE);
            module->screen_page = screen;

            switch (module->screen_page) {
                case 0:
                    _update_system_performance(GLOBAL_STATE);
                    break;
                case 1:
                    _update_system_info(GLOBAL_STATE);
                    break;
                case 2:
                    _update_esp32_info(GLOBAL_STATE);
                    break;
            }

            // Wait for 10 seconds or until a button press
            for (int i = 0; i < 10; i++) {
                if (xQueueReceive(user_input_queue, &input_event, pdMS_TO_TICKS(1000))) {
                    if (strcmp(input_event, "SHORT") == 0) {
                        ESP_LOGI(TAG, "Short button press detected, switching to next screen");
                        screen = (screen + 1) % 3; // Move to next screen
                        break;
                    } else if (strcmp(input_event, "LONG") == 0) {
                        ESP_LOGI(TAG, "Long button press detected, toggling WiFi SoftAP");
                        toggle_wifi_softap(); // Toggle AP 
                    }
                }
            }
        }
    }
}

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_accepted++;
    _update_shares(GLOBAL_STATE);
}
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_rejected++;
    _update_shares(GLOBAL_STATE);
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

    module->historical_hashrate[module->historical_hashrate_rolling_index] = GLOBAL_STATE->initial_ASIC_difficulty;
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

    _update_hashrate(GLOBAL_STATE);

    // logArrayContents(historical_hashrate, HISTORY_LENGTH);
    // logArrayContents(historical_hashrate_time_stamps, HISTORY_LENGTH);

    _check_for_best_diff(GLOBAL_STATE, found_diff, job_id);
}
