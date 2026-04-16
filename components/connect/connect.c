#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "esp_wifi_types_generic.h"

#include "connect.h"
#include "global_state.h"
#include "nvs_config.h"

// Maximum number of access points to scan
#define MAX_AP_COUNT 20

// Roaming configuration
#define ROAM_RSSI_THRESHOLD      (-67)         // RSSI low event triggers at this level (dBm)
#define ROAM_RSSI_HYSTERESIS     (8)           // New AP must be at least this much better (dB)
#define ROAM_BACKOFF_MS          (30 * 1000)   // Minimum time between roam scans
#define ROAM_COOLDOWN_MS         (120 * 1000)  // Don't roam again within this period after a successful roam

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

static const char * TAG = "connect";

static TimerHandle_t ip_acquire_timer = NULL;

static volatile bool is_scanning = false;
static uint16_t ap_number = 0;
static wifi_ap_record_t ap_info[MAX_AP_COUNT];
static int s_retry_num = 0;
static int clients_connected_to_ap = 0;

static const char *get_wifi_reason_string(int reason);
static void wifi_softap_on(void);
static void wifi_softap_off(void);

static GlobalState *roam_global_state = NULL;
static TimerHandle_t roam_retry_timer = NULL;
static volatile bool roam_in_progress = false;
static volatile bool is_roam_scan = false;
static wifi_ap_record_t roam_current_ap;
static uint8_t roam_target_bssid[6];
static TickType_t last_roam_scan_tick = 0;
static TickType_t last_roam_success_tick = 0;  // When did the last successful roam complete?
static volatile bool roam_just_connected = false; // True between roam connect and IP acquired

static void roam_trigger_scan(void);

// Periodic fallback: checks RSSI and scans if still below threshold.
// Needed because the RSSI low event only fires on a downward crossing —
// if we're persistently below threshold it won't re-fire.
static void roam_retry_timer_callback(TimerHandle_t xTimer)
{
    if (roam_global_state == NULL || !roam_global_state->SYSTEM_MODULE.is_connected) {
        return;
    }

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return;
    }

    if (ap.rssi >= ROAM_RSSI_THRESHOLD) {
        // Signal recovered, stop the retry timer and reset the cooldown
        ESP_LOGI(TAG, "Roam: RSSI recovered to %d dBm, stopping retry timer", ap.rssi);
        last_roam_success_tick = 0;  // Allow immediate roaming if signal drops again later
        xTimerStop(roam_retry_timer, 0);
        return;
    }

    ESP_LOGI(TAG, "Roam retry: still at %d dBm (threshold %d), triggering scan", ap.rssi, ROAM_RSSI_THRESHOLD);
    roam_trigger_scan();
}

static void roam_start_retry_timer(void)
{
    if (roam_retry_timer == NULL) {
        roam_retry_timer = xTimerCreate("roam_retry", pdMS_TO_TICKS(60 * 1000),
                                         pdTRUE, NULL, roam_retry_timer_callback);
    }
    if (roam_retry_timer != NULL) {
        xTimerStart(roam_retry_timer, 0);
    }
}

static void roam_trigger_scan(void)
{
    if (roam_global_state == NULL || !roam_global_state->SYSTEM_MODULE.is_connected) {
        return;
    }

    if (is_scanning || clients_connected_to_ap > 0) {
        return;
    }

    TickType_t now = xTaskGetTickCount();

    // Post-roam cooldown: don't scan again too soon after a successful roam
    if (last_roam_success_tick != 0 && (now - last_roam_success_tick) < pdMS_TO_TICKS(ROAM_COOLDOWN_MS)) {
        ESP_LOGD(TAG, "Roam scan skipped: post-roam cooldown active");
        return;
    }

    // Back-off: don't scan more often than ROAM_BACKOFF_MS
    if ((now - last_roam_scan_tick) < pdMS_TO_TICKS(ROAM_BACKOFF_MS)) {
        ESP_LOGD(TAG, "Roam scan skipped: backoff period active");
        return;
    }

    if (esp_wifi_sta_get_ap_info(&roam_current_ap) != ESP_OK) {
        return;
    }

    // If signal is fine, no need to scan
    if (roam_current_ap.rssi >= ROAM_RSSI_THRESHOLD) {
        return;
    }

    ESP_LOGI(TAG, "Roam: RSSI low (%d dBm), scanning for better AP...", roam_current_ap.rssi);

    // Ensure the retry timer is running while we're below threshold
    roam_start_retry_timer();

    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)roam_global_state->SYSTEM_MODULE.ssid,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    is_scanning = true;
    is_roam_scan = true;
    last_roam_scan_tick = now;
    if (esp_wifi_scan_start(&scan_config, false) != ESP_OK) {
        is_scanning = false;
        is_roam_scan = false;
    }
}

esp_err_t get_wifi_current_rssi(int8_t *rssi)
{
    wifi_ap_record_t current_ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&current_ap_info);

    if (err == ESP_OK) {
        *rssi = current_ap_info.rssi;
        return ERR_OK;
    }

    return err;
}

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

static void ip_timeout_callback(TimerHandle_t xTimer)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvTimerGetTimerID(xTimer);
    if (!GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        ESP_LOGI(TAG, "Timeout waiting for IP address. Disconnecting...");
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "IP Acquire Timeout");
        esp_wifi_disconnect();
    }
}

static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)arg;
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_SCAN_DONE) {
            ESP_LOGI(TAG, "Wi-Fi Scan Done");

            if (is_roam_scan) {
                // Roam-initiated scan: process results on heap to avoid stack overflow,
                // and consume them here so the regular ap_info path is not touched.
                is_roam_scan = false;
                uint16_t num_found = 0;
                esp_wifi_scan_get_ap_num(&num_found);

                if (num_found == 0) {
                    ESP_LOGI(TAG, "Roam scan: no APs found");
                    is_scanning = false;
                    return;
                }

                wifi_ap_record_t *scan_results = malloc(num_found * sizeof(wifi_ap_record_t));
                if (scan_results == NULL) {
                    ESP_LOGE(TAG, "Roam scan: failed to allocate %u record(s)", num_found);
                    is_scanning = false;
                    return;
                }

                if (esp_wifi_scan_get_ap_records(&num_found, scan_results) != ESP_OK) {
                    ESP_LOGE(TAG, "Roam scan: failed to get AP records");
                    free(scan_results);
                    is_scanning = false;
                    return;
                }
                is_scanning = false;

                // Find the best AP with matching SSID (excluding current BSSID)
                int8_t best_rssi = roam_current_ap.rssi;
                int best_idx = -1;
                for (int i = 0; i < num_found; i++) {
                    if (strcmp((char *)scan_results[i].ssid, roam_global_state->SYSTEM_MODULE.ssid) != 0) {
                        continue;
                    }
                    if (memcmp(scan_results[i].bssid, roam_current_ap.bssid, 6) == 0) {
                        continue;
                    }
                    if (scan_results[i].rssi > best_rssi) {
                        best_rssi = scan_results[i].rssi;
                        best_idx = i;
                    }
                }

                if (best_idx >= 0 && (best_rssi - roam_current_ap.rssi) >= ROAM_RSSI_HYSTERESIS) {
                    ESP_LOGI(TAG, "Roam: found better AP %02x:%02x:%02x:%02x:%02x:%02x (RSSI %d vs current %d), switching...",
                             scan_results[best_idx].bssid[0], scan_results[best_idx].bssid[1],
                             scan_results[best_idx].bssid[2], scan_results[best_idx].bssid[3],
                             scan_results[best_idx].bssid[4], scan_results[best_idx].bssid[5],
                             best_rssi, roam_current_ap.rssi);
                    memcpy(roam_target_bssid, scan_results[best_idx].bssid, 6);
                    roam_in_progress = true;
                    esp_wifi_disconnect();
                } else {
                    ESP_LOGI(TAG, "Roam check: no significantly better AP found (best candidate RSSI: %d, current: %d)",
                             best_rssi, roam_current_ap.rssi);
                }

                free(scan_results);

                // Re-arm the RSSI threshold only if we did NOT initiate a roam.
                // If we're roaming, the threshold will be re-armed after IP is acquired.
                if (!roam_in_progress) {
                    esp_wifi_set_rssi_threshold(ROAM_RSSI_THRESHOLD);
                }

                return;
            }

            // Regular user-initiated scan: store results in global ap_info for wifi_scan()
            esp_wifi_scan_get_ap_num(&ap_number);
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
            ESP_LOGI(TAG, "Connecting...");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Connecting...");
            esp_wifi_connect();
        }

        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "Acquiring IP...");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Acquiring IP...");

            if (ip_acquire_timer == NULL) {
                ip_acquire_timer = xTimerCreate("ip_acquire_timer", pdMS_TO_TICKS(30000), pdFALSE, (void *)GLOBAL_STATE, ip_timeout_callback);
            }
            if (ip_acquire_timer != NULL) {
                xTimerStart(ip_acquire_timer, 0);
            }            
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            if (event->reason == WIFI_REASON_ROAMING) {
                ESP_LOGI(TAG, "We are roaming, nothing to do");
                return;
            }

            // Handle roaming-initiated disconnect: reconnect to the specific better AP
            if (roam_in_progress) {
                roam_in_progress = false;
                GLOBAL_STATE->SYSTEM_MODULE.is_connected = false;

                ESP_LOGI(TAG, "Roaming reconnect: targeting AP %02x:%02x:%02x:%02x:%02x:%02x",
                         roam_target_bssid[0], roam_target_bssid[1],
                         roam_target_bssid[2], roam_target_bssid[3],
                         roam_target_bssid[4], roam_target_bssid[5]);
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.wifi_status), "Roaming...");

                // Set the target BSSID so we connect to the specific better AP
                wifi_config_t wifi_cfg;
                if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK) {
                    memcpy(wifi_cfg.sta.bssid, roam_target_bssid, 6);
                    wifi_cfg.sta.bssid_set = true;
                    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                }

                roam_just_connected = true;
                esp_wifi_connect();
                return;
            }

            ESP_LOGI(TAG, "Could not connect to '%.*s' [rssi %d]: reason %d", event->ssid_len, event->ssid, event->rssi, event->reason);
            if (clients_connected_to_ap > 0) {
                ESP_LOGI(TAG, "Client(s) connected to AP, not retrying...");
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.wifi_status), "Config AP connected!");
                return;
            }

            GLOBAL_STATE->SYSTEM_MODULE.is_connected = false;
            wifi_softap_on();

            snprintf(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.wifi_status), "%s (Error %d, retry #%d)", get_wifi_reason_string(event->reason), event->reason, s_retry_num);
            ESP_LOGI(TAG, "Wi-Fi status: %s", GLOBAL_STATE->SYSTEM_MODULE.wifi_status);

            // Wait a little
            vTaskDelay(5000 / portTICK_PERIOD_MS);

            s_retry_num++;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection...");

            // Clear any BSSID lock from a previous roam so reconnect uses signal-based selection
            wifi_config_t wifi_cfg;
            if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK && wifi_cfg.sta.bssid_set) {
                memset(wifi_cfg.sta.bssid, 0, 6);
                wifi_cfg.sta.bssid_set = false;
                esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
            }

            esp_wifi_connect();

            if (ip_acquire_timer != NULL) {
                xTimerStop(ip_acquire_timer, 0);
            }            
        }
        
        if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "Configuration Access Point enabled");
            GLOBAL_STATE->SYSTEM_MODULE.ap_enabled = true;
        }
                
        if (event_id == WIFI_EVENT_AP_STOP) {
            ESP_LOGI(TAG, "Configuration Access Point disabled");
            GLOBAL_STATE->SYSTEM_MODULE.ap_enabled = false;
        }

        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            clients_connected_to_ap += 1;
        }
        
        if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            clients_connected_to_ap -= 1;
        }

        if (event_id == WIFI_EVENT_STA_BSS_RSSI_LOW) {
            ESP_LOGI(TAG, "RSSI low event triggered, checking for better AP...");
            roam_trigger_scan();
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t * event = (ip_event_got_ip_t *) event_data;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str, IP4ADDR_STRLEN_MAX, IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "IPv4 Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str);
        s_retry_num = 0;

        xTimerStop(ip_acquire_timer, 0);
            if (ip_acquire_timer != NULL) {
        }

        GLOBAL_STATE->SYSTEM_MODULE.is_connected = true;

        ESP_LOGI(TAG, "Connected to SSID: %s", GLOBAL_STATE->SYSTEM_MODULE.ssid);
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Connected!");

        wifi_softap_off();

        // Enable RSSI monitoring so the driver fires WIFI_EVENT_STA_BSS_RSSI_LOW
        if (roam_global_state != NULL) {
            // If we just completed a roam, clean up the BSSID lock and record success
            if (roam_just_connected) {
                roam_just_connected = false;
                last_roam_success_tick = xTaskGetTickCount();

                // Clear bssid_set so future reconnects aren't locked to this AP
                wifi_config_t wifi_cfg;
                if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK && wifi_cfg.sta.bssid_set) {
                    memset(wifi_cfg.sta.bssid, 0, 6);
                    wifi_cfg.sta.bssid_set = false;
                    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                }

                ESP_LOGI(TAG, "Roam complete, cooldown active for %d seconds", ROAM_COOLDOWN_MS / 1000);
            }

            esp_wifi_set_rssi_threshold(ROAM_RSSI_THRESHOLD);
            ESP_LOGI(TAG, "RSSI threshold set to %d dBm for roaming", ROAM_RSSI_THRESHOLD);

            // The RSSI low event only fires on a downward crossing.
            // If we already connected with weak signal, trigger a scan now.
            // Skip this if we just roamed — the cooldown will handle further checks.
            if (last_roam_success_tick == 0) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.rssi < ROAM_RSSI_THRESHOLD) {
                    ESP_LOGI(TAG, "Already below RSSI threshold (%d dBm), triggering roam scan", ap.rssi);
                    roam_trigger_scan();
                }
            }
        }

        // Create IPv6 link-local address after WiFi connection
        esp_netif_t *netif = event->esp_netif;
        esp_err_t ipv6_err = esp_netif_create_ip6_linklocal(netif);
        if (ipv6_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create IPv6 link-local address: %s", esp_err_to_name(ipv6_err));
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t * event = (ip_event_got_ip6_t *) event_data;
        
        // Convert IPv6 address to string
        char ipv6_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &event->ip6_info.ip, ipv6_str, sizeof(ipv6_str));
        
        // Check if it's a link-local address (fe80::/10)
        if ((event->ip6_info.ip.addr[0] & 0xFFC0) == 0xFE80) {
            // For link-local addresses, append zone identifier using netif index
            int netif_index = esp_netif_get_netif_impl_index(event->esp_netif);
            if (netif_index >= 0) {
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str,
                        sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str),
                        "%s%%%d", ipv6_str, netif_index);
                ESP_LOGI(TAG, "IPv6 Link-Local Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str);
            } else {
                strncpy(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str, ipv6_str,
                       sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1);
                GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str[sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1] = '\0';
                ESP_LOGW(TAG, "IPv6 Link-Local Address: %s (could not get interface index)", ipv6_str);
            }
        } else {
            // Global or ULA address - no zone identifier needed
            strncpy(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str, ipv6_str,
                   sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1);
            GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str[sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1] = '\0';
            ESP_LOGI(TAG, "IPv6 Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str);
        }
    }
}

esp_netif_t * wifi_init_softap(GlobalState * GLOBAL_STATE)
{
    esp_netif_t * esp_netif_ap = esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    // Format the last 4 bytes of the MAC address as a hexadecimal string
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid, sizeof(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid), "Bitaxe_%02X%02X", mac[4], mac[5]);

    wifi_config_t wifi_ap_config = { 0 };
    wifi_ap_config.ap.ssid_len = strlen(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid);
    memcpy(wifi_ap_config.ap.ssid, GLOBAL_STATE->SYSTEM_MODULE.ap_ssid, wifi_ap_config.ap.ssid_len);
    wifi_ap_config.ap.channel = 1;
    wifi_ap_config.ap.max_connection = 10;
    wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_ap_config.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    return esp_netif_ap;
}

static bool is_wifi_operation_allowed(esp_err_t err)
{
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_STOP_STATE) {
        ESP_LOGI(TAG, "WiFi not initialized or stopped, skipping operation");
        return false;
    }
    return true;
}

void toggle_wifi_softap(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (is_wifi_operation_allowed(err)) {
        ESP_ERROR_CHECK(err);
    
        if (mode == WIFI_MODE_APSTA) {
            wifi_softap_off();
        } else {
            wifi_softap_on();
        }
    }
}

static void wifi_softap_off(void)
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (is_wifi_operation_allowed(err)) {
        ESP_ERROR_CHECK(err);
    }
}

static void wifi_softap_on(void)
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (is_wifi_operation_allowed(err)) {
        ESP_ERROR_CHECK(err);
    }
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

    size_t ssid_len = strlen(wifi_ssid);
    if (ssid_len > 32) ssid_len = 32;
    memcpy(wifi_sta_config.sta.ssid, wifi_ssid, ssid_len);
    if (ssid_len < 32) {
        wifi_sta_config.sta.ssid[ssid_len] = '\0';
    }

    if (authmode != WIFI_AUTH_OPEN) {
        strncpy((char *) wifi_sta_config.sta.password, wifi_pass, sizeof(wifi_sta_config.sta.password));
        wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password) - 1] = '\0';
    }
    // strncpy((char *) wifi_sta_config.sta.password, wifi_pass, 63);
    // wifi_sta_config.sta.password[63] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    // IPv6 link-local address will be created after WiFi connection
    
    // Start DHCP client for IPv4
    esp_netif_dhcpc_start(esp_netif_sta);

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    return esp_netif_sta;
}

void wifi_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_got_ip6;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, GLOBAL_STATE, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, GLOBAL_STATE, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler, GLOBAL_STATE, &instance_got_ip6));

    /* Initialize Wi-Fi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Use RAM-only storage so the driver doesn't persist/load stale BSSID data from NVS.
    // Without this, esp_wifi_connect() prefers the last-connected AP even when
    // a stronger one is available, defeating WIFI_CONNECT_AP_BY_SIGNAL on boot.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    wifi_softap_on();

    /* Initialize AP */
    wifi_init_softap(GLOBAL_STATE);

    GLOBAL_STATE->SYSTEM_MODULE.ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);

    /* Skip connection if SSID is null */
    if (strlen(GLOBAL_STATE->SYSTEM_MODULE.ssid) == 0) {
        ESP_LOGI(TAG, "No WiFi SSID provided, skipping connection");

        /* Start WiFi */
        ESP_ERROR_CHECK(esp_wifi_start());

        /* Disable power savings for best performance */
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        return;
    } else {

        char * wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS);

        /* Initialize STA */
        ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
        esp_netif_t * esp_netif_sta = wifi_init_sta(GLOBAL_STATE->SYSTEM_MODULE.ssid, wifi_pass);

        free(wifi_pass);

        /* Start Wi-Fi */
        ESP_ERROR_CHECK(esp_wifi_start());

        /* Disable power savings for best performance */
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        char * hostname  = nvs_config_get_string(NVS_CONFIG_HOSTNAME);

        /* Set Hostname */
        esp_err_t err = esp_netif_set_hostname(esp_netif_sta, hostname);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "esp_netif_set_hostname failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "ESP_WIFI setting hostname to: %s", hostname);
        }

        free(hostname);

        ESP_LOGI(TAG, "wifi_init_sta finished.");

        // Enable event-driven roaming (RSSI threshold set after IP is acquired)
        roam_global_state = GLOBAL_STATE;
        ESP_LOGI(TAG, "WiFi roaming enabled (RSSI threshold: %d dBm, hysteresis: %d dB)",
                 ROAM_RSSI_THRESHOLD, ROAM_RSSI_HYSTERESIS);

        return;
    }
}

typedef struct {
    int reason;
    const char *description;
} wifi_reason_desc_t;

static const wifi_reason_desc_t wifi_reasons[] = {
    {WIFI_REASON_UNSPECIFIED,                        "Unspecified reason"},
    {WIFI_REASON_AUTH_EXPIRE,                        "Authentication expired"},
    {WIFI_REASON_AUTH_LEAVE,                         "Deauthentication due to leaving"},
    {WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY,         "Disassociated due to inactivity"},
    {WIFI_REASON_ASSOC_TOOMANY,                      "Too many associated stations"},
    {WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA,      "Class 2 frame from non-authenticated STA"},
    {WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA,     "Class 3 frame from non-associated STA"},
    {WIFI_REASON_ASSOC_LEAVE,                        "Deassociated due to leaving"},
    {WIFI_REASON_ASSOC_NOT_AUTHED,                   "Association but not authenticated"},
    {WIFI_REASON_DISASSOC_PWRCAP_BAD,                "Disassociated due to poor power capability"},
    {WIFI_REASON_DISASSOC_SUPCHAN_BAD,               "Disassociated due to unsupported channel"},
    {WIFI_REASON_BSS_TRANSITION_DISASSOC,            "Disassociated due to BSS transition"},
    {WIFI_REASON_IE_INVALID,                         "Invalid Information Element"},
    {WIFI_REASON_MIC_FAILURE,                        "MIC failure detected"},
    {WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,             "Incorrect password entered"}, // 4-way handshake timeout
    {WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT,           "Group key update timeout"},
    {WIFI_REASON_IE_IN_4WAY_DIFFERS,                 "IE differs in 4-way handshake"},
    {WIFI_REASON_GROUP_CIPHER_INVALID,               "Invalid group cipher"},
    {WIFI_REASON_PAIRWISE_CIPHER_INVALID,            "Invalid pairwise cipher"},
    {WIFI_REASON_AKMP_INVALID,                       "Invalid AKMP"},
    {WIFI_REASON_UNSUPP_RSN_IE_VERSION,              "Unsupported RSN IE version"},
    {WIFI_REASON_INVALID_RSN_IE_CAP,                 "Invalid RSN IE capabilities"},
    {WIFI_REASON_802_1X_AUTH_FAILED,                 "802.1X authentication failed"},
    {WIFI_REASON_CIPHER_SUITE_REJECTED,              "Cipher suite rejected"},
    {WIFI_REASON_TDLS_PEER_UNREACHABLE,              "TDLS peer unreachable"},
    {WIFI_REASON_TDLS_UNSPECIFIED,                   "TDLS unspecified error"},
    {WIFI_REASON_SSP_REQUESTED_DISASSOC,             "SSP requested disassociation"},
    {WIFI_REASON_NO_SSP_ROAMING_AGREEMENT,           "No SSP roaming agreement"},
    {WIFI_REASON_BAD_CIPHER_OR_AKM,                  "Bad cipher or AKM"},
    {WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION,       "Not authorized in this location"},
    {WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS,        "Service change precludes TS"},
    {WIFI_REASON_UNSPECIFIED_QOS,                    "Unspecified QoS reason"},
    {WIFI_REASON_NOT_ENOUGH_BANDWIDTH,               "Not enough bandwidth"},
    {WIFI_REASON_MISSING_ACKS,                       "Missing ACKs"},
    {WIFI_REASON_EXCEEDED_TXOP,                      "Exceeded TXOP"},
    {WIFI_REASON_STA_LEAVING,                        "Station leaving"},
    {WIFI_REASON_END_BA,                             "End of Block Ack"},
    {WIFI_REASON_UNKNOWN_BA,                         "Unknown Block Ack"},
    {WIFI_REASON_TIMEOUT,                            "Timeout occured"},
    {WIFI_REASON_PEER_INITIATED,                     "Peer-initiated disassociation"},
    {WIFI_REASON_AP_INITIATED,                       "Access Point-initiated disassociation"},
    {WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT,      "Invalid FT action frame count"},
    {WIFI_REASON_INVALID_PMKID,                      "Invalid PMKID"},
    {WIFI_REASON_INVALID_MDE,                        "Invalid MDE"},
    {WIFI_REASON_INVALID_FTE,                        "Invalid FTE"},
    {WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED, "Transmission link establishment failed"},
    {WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED,        "Alternative channel occupied"},
    {WIFI_REASON_BEACON_TIMEOUT,                     "Beacon timeout"},
    {WIFI_REASON_NO_AP_FOUND,                        "No access point found"},
    {WIFI_REASON_AUTH_FAIL,                          "Authentication failed"},
    {WIFI_REASON_ASSOC_FAIL,                         "Association failed"},
    {WIFI_REASON_HANDSHAKE_TIMEOUT,                  "Handshake timeout"},
    {WIFI_REASON_CONNECTION_FAIL,                    "Connection failed"},
    {WIFI_REASON_AP_TSF_RESET,                       "Access point TSF reset"},
    {WIFI_REASON_ROAMING,                            "Roaming in progress"},
    {WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG,       "Association comeback time too long"},
    {WIFI_REASON_SA_QUERY_TIMEOUT,                   "SA query timeout"},
    {WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY,  "No access point found with compatible security"},
    {WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD,  "No access point found in auth mode threshold"},
    {WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD,      "No access point found in RSSI threshold"},
    {0,                                               NULL},
};

static const char *get_wifi_reason_string(int reason) {
    for (int i = 0; wifi_reasons[i].reason != 0; i++) {
        if (wifi_reasons[i].reason == reason) {
            return wifi_reasons[i].description;
        }
    }
    return "Unknown error";
}
