#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "lwip/lwip_napt.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi_types_generic.h"

#include "connect.h"
#include "connect_wifi.h"
#include "usb_net.h"
#include "global_state.h"
#include "nvs_config.h"

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

static const char * TAG = "connect";

static TimerHandle_t ip_acquire_timer = NULL;

static bool is_scanning = false;
static uint16_t ap_number = 0;
static wifi_ap_record_t ap_info[MAX_AP_COUNT];
static int s_retry_num = 0;
static int clients_connected_to_ap = 0;
static bool mdns_initialized = false;

static const char *get_wifi_reason_string(int reason);

static char* generate_unique_hostname(const char *base);
static char* check_and_resolve_hostname_conflict(const char *hostname, const char *current_ip);

void initialize_mdns_if_needed(void *pvParameters, esp_netif_t *netif) {
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    
    if (hostname == NULL) {
        ESP_LOGW(TAG, "Hostname not configured, skipping mDNS setup");
        return;
    }

    if (!mdns_initialized) {
        esp_err_t err = mdns_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS/Avahi initialization failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "mDNS/Avahi initialized successfully");

            /* Get current IP from the provided netif */
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(netif, &ip_info);
            char current_ip[16];
            snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));

            /* Check for hostname conflicts and set global hostname */
            char *final_hostname = check_and_resolve_hostname_conflict(hostname, current_ip);
            ESP_LOGI(TAG, "mDNS initializing with IP: %s and hostname: %s", current_ip, final_hostname);
            mdns_hostname_set(final_hostname);
            strncpy(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname, final_hostname, sizeof(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname) - 1);
            GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname[sizeof(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname) - 1] = '\0';
            
            /* Set mDNS instance name using base MAC */
            uint8_t mac[6];
            esp_efuse_mac_get_default(mac);
            char mac_suffix[6];
            snprintf(mac_suffix, sizeof(mac_suffix), "%02X%02X", mac[4], mac[5]);

            char instance_name[64];
            snprintf(instance_name, sizeof(instance_name), "Bitaxe %s %s (%s)",
                     GLOBAL_STATE->DEVICE_CONFIG.family.name,
                     GLOBAL_STATE->DEVICE_CONFIG.board_version,
                     mac_suffix);
            
            /* Add HTTP service */
            mdns_service_add(instance_name, "_http", "_tcp", 80, NULL, 0);
            
            mdns_initialized = true;
            free(final_hostname);
        }
    }

    /* 
     * Explicitly register this interface with mDNS and trigger an announcement.
     * This is required for custom interfaces like "USB_NCM" to be recognized.
     * We must both ENABLE and ANNOUNCE the protocols.
     */
    esp_err_t reg_err = mdns_register_netif(netif);
    if (reg_err == ESP_OK || reg_err == ESP_ERR_INVALID_STATE) {
        mdns_netif_action(netif, MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ENABLE_IP6 | 
                                 MDNS_EVENT_ANNOUNCE_IP4 | MDNS_EVENT_ANNOUNCE_IP6);
        ESP_LOGI(TAG, "mDNS enabled and announced on interface %p", netif);
    } else {
        ESP_LOGW(TAG, "Failed to register netif with mDNS: %s", esp_err_to_name(reg_err));
    }

    free(hostname);
}

esp_err_t update_mdns_hostname(void *pvParameters, const char *new_hostname) {
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    if (new_hostname == NULL || strlen(new_hostname) == 0) {
        ESP_LOGW(TAG, "Invalid hostname provided for mDNS update");
        return ESP_ERR_INVALID_ARG;
    }

    /* Get the correct netif based on network mode */
    const char *ifkey = (GLOBAL_STATE->SYSTEM_MODULE.network_mode == NETWORK_MODE_USB) ? "USB_NCM" : "WIFI_STA_DEF";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);

    /* Get current IP for conflict checking */
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    char current_ip[16];
    snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));

    /* Check for hostname conflicts and resolve if needed */
    char *resolved_hostname = check_and_resolve_hostname_conflict(new_hostname, current_ip);

    /* If the hostname was resolved to a different one, update NVS */
    if (strcmp(resolved_hostname, new_hostname) != 0) {
        nvs_config_set_string(NVS_CONFIG_HOSTNAME, resolved_hostname);
        ESP_LOGI(TAG, "Hostname conflict resolved, updated NVS to: %s", resolved_hostname);
    }

    esp_err_t err = mdns_hostname_set(resolved_hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update mDNS hostname to: %s, error: %s", resolved_hostname, esp_err_to_name(err));
        free(resolved_hostname);
        return err;
    }

    ESP_LOGI(TAG, "mDNS hostname updated to: %s", resolved_hostname);
    strncpy(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname, resolved_hostname, sizeof(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname) - 1);
    GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname[sizeof(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname) - 1] = '\0';
    free(resolved_hostname);
    return ESP_OK;
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
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "IP Acquire Timeout");
        esp_wifi_disconnect();
    }
}

static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)arg;
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
            ESP_LOGI(TAG, "Connecting...");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "Connecting...");
            esp_wifi_connect();
        }

        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "Acquiring IP...");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "Acquiring IP...");

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

            ESP_LOGI(TAG, "Could not connect to '%.*s' [rssi %d]: reason %d", event->ssid_len, event->ssid, event->rssi, event->reason);
            if (clients_connected_to_ap > 0) {
                ESP_LOGI(TAG, "Client(s) connected to AP, not retrying...");
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.network_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.network_status), "Config AP connected!");
                return;
            }

            snprintf(GLOBAL_STATE->SYSTEM_MODULE.network_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.network_status), "%s (Error %d, retry #%d)", get_wifi_reason_string(event->reason), event->reason, s_retry_num);
            ESP_LOGI(TAG, "Wi-Fi status: %s", GLOBAL_STATE->SYSTEM_MODULE.network_status);

            // Wait a little
            vTaskDelay(5000 / portTICK_PERIOD_MS);

            s_retry_num++;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
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
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "Connected!");
        
        // Create IPv6 link-local address after WiFi connection
        esp_netif_t *netif = event->esp_netif;
        esp_err_t ipv6_err = esp_netif_create_ip6_linklocal(netif);
        if (ipv6_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create IPv6 link-local address: %s", esp_err_to_name(ipv6_err));
        }

        initialize_mdns_if_needed(GLOBAL_STATE, event->esp_netif);
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

        initialize_mdns_if_needed(GLOBAL_STATE, event->esp_netif);
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
        switch (mode) {
            case WIFI_MODE_NULL:
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
                break;
            case WIFI_MODE_STA:
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
                break;
            case WIFI_MODE_AP:
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
                break;
            case WIFI_MODE_APSTA:
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                break;
            default:
                break;
        }
    }
}


static char* generate_unique_hostname(const char *base) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char suffix[6];
    snprintf(suffix, sizeof(suffix), "-%02x%02x", mac[4], mac[5]);
    char *new_hostname = malloc(strlen(base) + strlen(suffix) + 1);
    strcpy(new_hostname, base);
    strcat(new_hostname, suffix);
    return new_hostname;
}

static char* check_and_resolve_hostname_conflict(const char *hostname, const char *current_ip) {
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_generic(hostname, NULL, NULL, MDNS_TYPE_A, MDNS_QUERY_MULTICAST, 3000, 1, &results);
    if (err != ESP_OK || !results || !results->addr) {
        // No A record found, no conflict
        if (results) mdns_query_results_free(results);
        return strdup(hostname);
    }

    mdns_ip_addr_t *a = results->addr;
    char ip_str[INET6_ADDRSTRLEN];
    if (a->addr.type == IPADDR_TYPE_V4) {
        esp_ip4addr_ntoa(&a->addr.u_addr.ip4, ip_str, sizeof(ip_str));
    } else {
        inet_ntop(AF_INET6, &a->addr.u_addr.ip6, ip_str, sizeof(ip_str));
    }

    if (strcmp(ip_str, current_ip) != 0) {
        // Different IP, conflict detected
        char *new_hostname = generate_unique_hostname(hostname);
        ESP_LOGI(TAG, "mDNS conflict detected for '%s' at %s, renaming to '%s'", hostname, ip_str, new_hostname);
        nvs_config_set_string(NVS_CONFIG_HOSTNAME, new_hostname);
        mdns_query_results_free(results);
        return new_hostname;
    }

    mdns_query_results_free(results);
    return strdup(hostname);
}

void connect_init(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_instance_t instance_any_id, instance_got_ip, instance_got_ip6;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, GLOBAL_STATE, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, GLOBAL_STATE, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler, GLOBAL_STATE, &instance_got_ip6));

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure and start SoftAP
    esp_err_t err = esp_wifi_set_mode(GLOBAL_STATE->SYSTEM_MODULE.network_mode == NETWORK_MODE_WIFI ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (is_wifi_operation_allowed(err)) {
        ESP_ERROR_CHECK(err);
    }

    // Initialize SoftAP
    wifi_init_softap(GLOBAL_STATE);

    // Configure STA BEFORE starting WiFi to avoid race condition
    if (GLOBAL_STATE->SYSTEM_MODULE.network_mode == NETWORK_MODE_WIFI) {
        ESP_LOGI(TAG, "Network mode: WiFi");

        // Skip connection if SSID is null
        if (strlen(GLOBAL_STATE->SYSTEM_MODULE.ssid) == 0) {
            ESP_LOGI(TAG, "No WiFi SSID provided, running in AP-only mode");
        } else {
            ESP_LOGI(TAG, "Configuring Wi-Fi STA");
            connect_wifi_init(GLOBAL_STATE);
        }
    }

    // Start Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
    // Disable power savings for best performance
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (GLOBAL_STATE->SYSTEM_MODULE.network_mode == NETWORK_MODE_USB) {
        ESP_LOGI(TAG, "Network mode: Ethernet-over-USB");
        usb_net_init(GLOBAL_STATE);
    }

    ESP_LOGI(TAG, "Network initialization complete");
}

void connect_await_connection(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // Wait for connection to be established
    ESP_LOGI(TAG, "Waiting for network connection...");
    while (!GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Disable AP after connection is established
    ESP_LOGI(TAG, "Connection established, disabling AP");
    if (GLOBAL_STATE->SYSTEM_MODULE.network_mode == NETWORK_MODE_WIFI) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    } else if (GLOBAL_STATE->SYSTEM_MODULE.network_mode == NETWORK_MODE_USB) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
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
