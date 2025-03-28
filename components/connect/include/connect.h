#pragma once

#include "lwip/sys.h"
#include <arpa/inet.h>
#include <lwip/netdb.h>

#include "freertos/event_groups.h"
#include "esp_wifi_types.h"

// Structure to hold WiFi scan results
typedef struct {
    char ssid[33];  // 32 chars + null terminator
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_simple_t;

#define WIFI_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define HOSTNAME CONFIG_LWIP_LOCAL_HOSTNAME



/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// enum of wifi statuses
typedef enum
{
    WIFI_CONNECTED,
    WIFI_CONNECTING,
    WIFI_RETRYING,
} wifi_status_t;

void toggle_wifi_softap(void);
void wifi_softap_on(void);
void wifi_softap_off(void);
void wifi_init(const char * wifi_ssid, const char * wifi_pass, const char * hostname, char * ip_addr_str);
EventBits_t wifi_connect(void);
void generate_ssid(char * ssid);
esp_err_t wifi_scan(wifi_ap_record_simple_t *ap_records, uint16_t *ap_count);