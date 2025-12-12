#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "connect_wifi.h"
#include "global_state.h"
#include "nvs_config.h"

static const char *TAG = "connect_wifi";

static esp_netif_t *wifi_init_sta(const char *wifi_ssid, const char *wifi_pass)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

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

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    // IPv6 link-local address will be created after WiFi connection

    // Start DHCP client for IPv4
    esp_netif_dhcpc_start(esp_netif_sta);

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    return esp_netif_sta;
}

esp_err_t connect_wifi_init(GlobalState *GLOBAL_STATE)
{
    char *wifi_ssid = GLOBAL_STATE->SYSTEM_MODULE.ssid;
    char *wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS);

    ESP_LOGI(TAG, "Initializing WiFi STA mode");
    esp_netif_t *esp_netif_sta = wifi_init_sta(wifi_ssid, wifi_pass);

    free(wifi_pass);

    /* Set Hostname */
    char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    esp_err_t err = esp_netif_set_hostname(esp_netif_sta, hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_netif_set_hostname failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Setting hostname to: %s", hostname);
    }
    free(hostname);

    ESP_LOGI(TAG, "WiFi STA initialization complete");

    return ESP_OK;
}
