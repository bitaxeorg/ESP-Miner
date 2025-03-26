
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/lwip_napt.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include <string.h>

#include "connect.h"
#include "main.h"

// Maximum number of access points to scan
#define MAX_AP_COUNT 20

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""

#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID

#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char * TAG = "wifi_station";

static bool is_scanning = false;
static uint16_t ap_number = 0;
static wifi_ap_record_t ap_info[MAX_AP_COUNT];

// Function to scan for available WiFi networks
esp_err_t wifi_scan(wifi_ap_record_simple_t *ap_records, uint16_t *ap_count)
{
    if (is_scanning) {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi scan!");
    is_scanning = true;

    wifi_ap_record_t current_ap_info;
    if (esp_wifi_sta_get_ap_info(&current_ap_info) != ESP_OK) {
        ESP_LOGI(TAG, "Forcing disconnect so that we can scan!");
        esp_wifi_disconnect();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

     wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan start failed with error: %s", esp_err_to_name(err));
        is_scanning = false;
        return err;
    }

    uint16_t retries_remaining = 10;
    while (is_scanning) {
        retries_remaining--;
        if (retries_remaining == 0) {
            is_scanning = false;
            return ESP_FAIL;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(TAG, "Wi-Fi networks found: %d", ap_number);
    if (ap_number == 0) {
        ESP_LOGW(TAG, "No Wi-Fi networks found");
    }

    *ap_count = ap_number;
    memset(ap_records, 0, (*ap_count) * sizeof(wifi_ap_record_simple_t));
    for (int i = 0; i < ap_number; i++) {
        memcpy(ap_records[i].ssid, ap_info[i].ssid, sizeof(ap_records[i].ssid));
        ap_records[i].rssi = ap_info[i].rssi;
        ap_records[i].authmode = ap_info[i].authmode;
    }

    ESP_LOGD(TAG, "Finished Wi-Fi scan!");

    return ESP_OK;
}

static int s_retry_num = 0;

static char * _ip_addr_str;

static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_SCAN_DONE) {
            esp_wifi_scan_get_ap_num(&ap_number);
            ESP_LOGI(TAG, "Wi-Fi Scan Done");
            if (esp_wifi_scan_get_ap_records(&ap_number, ap_info) != ESP_OK) {
                ESP_LOGI(TAG, "Failed esp_wifi_scan_get_ap_records");
            }
            is_scanning = false;
        }

        if (is_scanning) {
            ESP_LOGI(TAG, "Still scanning, ignore wifi event.");
            return;
        }

        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
            MINER_set_wifi_status(WIFI_CONNECTING, 0, 0);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            if (event->reason == WIFI_REASON_ROAMING) {
                ESP_LOGI(TAG, "We are roaming, nothing to do");
                return;
            }

            ESP_LOGI(TAG, "Could not connect to '%s' [rssi %d]: reason %d", event->ssid, event->rssi, event->reason);

            // Wait a little
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
            MINER_set_wifi_status(WIFI_RETRYING, s_retry_num, event->reason);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t * event = (ip_event_got_ip_t *) event_data;
        snprintf(_ip_addr_str, IP4ADDR_STRLEN_MAX, IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "Bitaxe ip: %s", _ip_addr_str);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        MINER_set_wifi_status(WIFI_CONNECTED, 0, 0);
    }
}

void generate_ssid(char * ssid)
{
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    // Format the last 4 bytes of the MAC address as a hexadecimal string
    snprintf(ssid, 32, "Bitaxe_%02X%02X", mac[4], mac[5]);
}

esp_netif_t * wifi_init_softap(void)
{
    esp_netif_t * esp_netif_ap = esp_netif_create_default_wifi_ap();

    // Define a buffer for the SSID
    char ssid_with_mac[13]; // "Bitaxe" + 4 bytes from MAC address

    // Generate the SSID
    generate_ssid(ssid_with_mac);

    wifi_config_t wifi_ap_config;
    memset(&wifi_ap_config, 0, sizeof(wifi_ap_config)); // Clear the structure
    strncpy((char *) wifi_ap_config.ap.ssid, ssid_with_mac, sizeof(wifi_ap_config.ap.ssid));
    wifi_ap_config.ap.ssid_len = strlen(ssid_with_mac);
    wifi_ap_config.ap.channel = 1;
    wifi_ap_config.ap.max_connection = 10;
    wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_ap_config.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    return esp_netif_ap;
}

void toggle_wifi_softap(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

    if (mode == WIFI_MODE_APSTA) {
        wifi_softap_off();
    } else {
        wifi_softap_on();
    }
}

void wifi_softap_off(void)
{
    ESP_LOGI(TAG, "ESP_WIFI Access Point Off");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    MINER_set_ap_status(false);
}

void wifi_softap_on(void)
{
    ESP_LOGI(TAG, "ESP_WIFI Access Point On");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    MINER_set_ap_status(true);
}

/* Initialize wifi station */
esp_netif_t * wifi_init_sta(const char * wifi_ssid, const char * wifi_pass)
{
    esp_netif_t * esp_netif_sta = esp_netif_create_default_wifi_sta();

    /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
    * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
    * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
    * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
    */
    wifi_auth_mode_t authmode;

    if (strlen(wifi_pass) == 0) {
        ESP_LOGI(TAG, "No Wi-Fi password provided, using open network");
        authmode = WIFI_AUTH_OPEN;
    } else {
        ESP_LOGI(TAG, "Wi-Fi Password provided, using WPA2");
        authmode = WIFI_AUTH_WPA2_PSK;
    }

    wifi_config_t wifi_sta_config = {
        .sta =
            {
                .threshold.authmode = authmode,
                .btm_enabled = 1,
                .rm_enabled = 1,
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .pmf_cfg =
                    {
                        .capable = true,
                        .required = false
                    },
        },
    };

    strncpy((char *) wifi_sta_config.sta.ssid, wifi_ssid, sizeof(wifi_sta_config.sta.ssid));
    wifi_sta_config.sta.ssid[sizeof(wifi_sta_config.sta.ssid) - 1] = '\0';

    if (authmode != WIFI_AUTH_OPEN) {
        strncpy((char *) wifi_sta_config.sta.password, wifi_pass, sizeof(wifi_sta_config.sta.password));
        wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password) - 1] = '\0';
    }
    // strncpy((char *) wifi_sta_config.sta.password, wifi_pass, 63);
    // wifi_sta_config.sta.password[63] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    return esp_netif_sta;
}

void wifi_init(const char * wifi_ssid, const char * wifi_pass, const char * hostname, char * ip_addr_str)
{
    _ip_addr_str = ip_addr_str;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    /* Initialize Wi-Fi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_softap_on();

    /* Initialize AP */
    ESP_LOGI(TAG, "ESP_WIFI Access Point On");
    wifi_init_softap();


    /* Skip connection if SSID is null */
    if (strlen(wifi_ssid) == 0) {
        ESP_LOGI(TAG, "No WiFi SSID provided, skipping connection");

        /* Start WiFi */
        ESP_ERROR_CHECK(esp_wifi_start());

        /* Disable power savings for best performance */
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        return;
    } else {
        /* Initialize STA */
        ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
        esp_netif_t * esp_netif_sta = wifi_init_sta(wifi_ssid, wifi_pass);

        /* Start Wi-Fi */
        ESP_ERROR_CHECK(esp_wifi_start());

        /* Disable power savings for best performance */
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        /* Set Hostname */
        esp_err_t err = esp_netif_set_hostname(esp_netif_sta, hostname);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "esp_netif_set_hostname failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "ESP_WIFI setting hostname to: %s", hostname);
        }

        ESP_LOGI(TAG, "wifi_init_sta finished.");

        return;
    }
}

EventBits_t wifi_connect(void)
{
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */

    return bits;
}