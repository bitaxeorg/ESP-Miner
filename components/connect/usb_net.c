#include "usb_net.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_event.h"
#include "lwip/sockets.h"
#include "tinyusb_default_config.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

#include "tinyusb_net.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb.h"
#include "global_state.h"
#include "nvs_config.h"

static const char *TAG = "usb_net";

// Static configuration structures
static esp_netif_driver_ifconfig_t driver_cfg;
static esp_netif_inherent_config_t netif_cfg;

// Static configuration structures

/**
 * @brief Transmit function for sending packets to USB
 */
static esp_err_t netif_transmit(void *h, void *buffer, size_t len)
{
    esp_err_t err = tinyusb_net_send_sync(buffer, len, NULL, pdMS_TO_TICKS(100));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE during startup is expected - USB link not ready yet
        // DHCP will retry automatically, so don't spam logs
        ESP_LOGE(TAG, "Failed to send buffer to USB: %s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Free function for RX buffers
 */
static void l2_free(void *h, void *buffer)
{
    free(buffer);
}

/**
 * @brief Callback when USB receives data from host
 */
static esp_err_t netif_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    esp_netif_t *netif = (esp_netif_t *)ctx;
    if (!netif) {
        return ESP_OK;
    }

    // Copy buffer because TinyUSB will reuse it
    void *buf_copy = malloc(len);
    if (!buf_copy) {
        ESP_LOGE(TAG, "Failed to allocate RX buffer");
        return ESP_ERR_NO_MEM;
    }

    memcpy(buf_copy, buffer, len);
    return esp_netif_receive(netif, buf_copy, len, NULL);
}

// Callback for CDC ACM when line state changes (DTR, RTS)
static void cdc_acm_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        cdcacm_event_line_state_changed_data_t line_state = event->line_state_changed_data;
        ESP_LOGI(TAG, "CDC ACM Line state changed - Interface: %d, DTR: %s, RTS: %s",
                  itf, line_state.dtr ? "true" : "false", line_state.rts ? "true" : "false");
    }
}

// Event handler for Ethernet-over-USB IP events
static void usb_ip_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

        // Update global state with IP address
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str, IP4ADDR_STRLEN_MAX,
                 IPSTR, IP2STR(&event->ip_info.ip));
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "Connected!");
        GLOBAL_STATE->SYSTEM_MODULE.is_connected = true;

        // Create IPv6 link-local address after USB connection
        esp_netif_t *netif = event->esp_netif;
        esp_err_t ipv6_err = esp_netif_create_ip6_linklocal(netif);
        if (ipv6_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create IPv6 link-local address: %s", esp_err_to_name(ipv6_err));
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t* event = (ip_event_got_ip6_t*) event_data;

        // Convert IPv6 address to string
        char ipv6_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &event->ip6_info.ip, ipv6_str, sizeof(ipv6_str));

        // Check if it's a link-local address (fe80::/10)
        if ((event->ip6_info.ip.addr[0] & 0xFFC0) == 0xFE80) {
            // Store link-local IPv6 address
            strncpy(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str, ipv6_str, sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1);
            GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str[sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1] = '\0';
            ESP_LOGI(TAG, "IPv6 Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str);
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGI(TAG, "Lost IP");
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "Lost IP");
        GLOBAL_STATE->SYSTEM_MODULE.is_connected = false;
        GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str[0] = '\0';
        GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str[0] = '\0';
    }
}

void usb_net_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    esp_event_handler_instance_t instance_got_ip_usb;
    esp_event_handler_instance_t instance_got_ip6_usb;
    esp_event_handler_instance_t instance_lost_ip_usb;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &usb_ip_event_handler, GLOBAL_STATE, &instance_got_ip_usb));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &usb_ip_event_handler, GLOBAL_STATE, &instance_got_ip6_usb));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &usb_ip_event_handler, GLOBAL_STATE, &instance_lost_ip_usb));

    char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);

    // Build USB string descriptors
    static char *string_descriptors[4];
    string_descriptors[0] = "\x09\x04"; // Language ID: English (US)

    string_descriptors[1] = "ESP-Miner"; // Manufacturer

    static char product_str[64];
    snprintf(product_str, 64, "Bitaxe %s %s (%s)", GLOBAL_STATE->DEVICE_CONFIG.family.name, GLOBAL_STATE->DEVICE_CONFIG.board_version, hostname);
    string_descriptors[2] = product_str; // Product

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    static char serial_str[13];
    snprintf(serial_str, 13, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    string_descriptors[3] = serial_str; // Serial

    // Initialize TinyUSB driver (descriptors from sdkconfig)
    ESP_LOGI(TAG, "Initializing TinyUSB driver");
    tinyusb_config_t usb_cfg = TINYUSB_DEFAULT_CONFIG();
    usb_cfg.descriptor.string = (const char **)string_descriptors;
    usb_cfg.descriptor.string_count = 4;
    usb_cfg.task = TINYUSB_TASK_CUSTOM(4096, 5, 0);  // 4KB stack, priority 5, CPU0

    ESP_ERROR_CHECK(tinyusb_driver_install(&usb_cfg));

    ESP_LOGI(TAG, "Initializing CDC ACM for serial communication");
    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = &cdc_acm_line_state_changed_callback,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
    ESP_ERROR_CHECK(tinyusb_console_init(TINYUSB_CDC_ACM_0));

    // Setup netif inherent config
    netif_cfg = (esp_netif_inherent_config_t)ESP_NETIF_INHERENT_DEFAULT_ETH();
    netif_cfg.flags = ESP_NETIF_FLAG_AUTOUP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED;
    netif_cfg.if_key = "USB_NCM";
    netif_cfg.if_desc = "usb ncm network device";
    netif_cfg.route_prio = 10;

    // Setup driver config
    driver_cfg = (esp_netif_driver_ifconfig_t){
        .handle = (void *)1,  // TinyUSB NCM is a singleton, just needs non-NULL
        .transmit = netif_transmit,
        .driver_free_rx_buffer = l2_free
    };

    // Create netif configuration
    esp_netif_config_t cfg = {
        .base = &netif_cfg,
        .driver = &driver_cfg,
        .stack = _g_esp_netif_netstack_default_eth,
    };

    // Create the network interface
    esp_netif_t *netif = esp_netif_new(&cfg);
    if (!netif) {
        ESP_LOGE(TAG, "Failed to create netif");
        return;
    }

    // Initialize TinyUSB Net
    ESP_LOGI(TAG, "Initializing TinyUSB NCM");
    tinyusb_net_config_t net_cfg = {
        .on_recv_callback = netif_recv_callback,
        .user_context = netif,
    };

    // Get and derive MAC address from eFuse
    uint8_t efuse_mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(efuse_mac));

    ESP_ERROR_CHECK(esp_derive_local_mac(net_cfg.mac_addr, efuse_mac));

    ESP_LOGI(TAG, "USB NCM MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             net_cfg.mac_addr[0], net_cfg.mac_addr[1], net_cfg.mac_addr[2],
             net_cfg.mac_addr[3], net_cfg.mac_addr[4], net_cfg.mac_addr[5]);

    ESP_ERROR_CHECK(tinyusb_net_init(&net_cfg));

    // The netif callbacks are already connected via the driver_cfg we passed to esp_netif_new
    // TinyUSB will call netif_recv_callback when data arrives
    // DHCP will call netif_transmit via esp_netif when it needs to send packets

    // Manually start the interface
    ESP_LOGI(TAG, "Starting network interface");
    esp_netif_action_start(netif, NULL, 0, NULL);

    // Mark interface as connected (link up)
    esp_netif_action_connected(netif, NULL, 0, NULL);

    // Give USB a moment to stabilize before starting DHCP
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start the DHCP client
    ESP_LOGI(TAG, "Starting DHCP client for Ethernet-over-USB");
    ESP_ERROR_CHECK(esp_netif_dhcpc_start(netif));

    ESP_LOGI(TAG, "Ethernet-over-USB initialized successfully - waiting for IP from DHCP");

    strcpy(GLOBAL_STATE->SYSTEM_MODULE.network_status, "Acquiring IP...");
}
