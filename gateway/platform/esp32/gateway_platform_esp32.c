/**
 * gateway_platform_esp32.c — ESP-IDF implementation of gateway_platform.h
 *
 * All ESP-IDF / FreeRTOS / lwIP calls are confined to this file.
 * The gateway core (gateway_core.c, miner_adapter.c) only calls platform_*().
 */

#include "gateway_platform.h"
#include "gateway_types.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "heap_memory_layout.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

// ── Logging ──────────────────────────────────────────────────────────────────

static const esp_log_level_t log_level_map[] = {
    ESP_LOG_ERROR,   // GW_LOG_ERROR = 0
    ESP_LOG_WARN,    // GW_LOG_WARN  = 1
    ESP_LOG_INFO,    // GW_LOG_INFO  = 2
    ESP_LOG_DEBUG,   // GW_LOG_DEBUG = 3
};

void platform_log(int level, const char *tag, const char *fmt, ...)
{
    if (level < 0 || level > 3) level = 0;
    esp_log_level_t esp_level = log_level_map[level];

    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ESP_LOG_LEVEL(esp_level, tag, "%s", buf);
}

// ── Timing & system ──────────────────────────────────────────────────────────

int64_t platform_time_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000LL);
}

void platform_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

size_t platform_get_free_heap(void)
{
    return (size_t)esp_get_free_heap_size();
}

void platform_reboot(void)
{
    esp_restart();
}

// ── Network identity ─────────────────────────────────────────────────────────

void platform_get_mac_str(char *buf, size_t buf_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void platform_get_local_ip(char *buf, size_t buf_size)
{
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif) {
        strncpy(buf, "0.0.0.0", buf_size);
        return;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        strncpy(buf, "0.0.0.0", buf_size);
        return;
    }
    esp_ip4addr_ntoa(&ip_info.ip, buf, buf_size);
}

void platform_get_netmask(char *buf, size_t buf_size)
{
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif) {
        strncpy(buf, "255.255.255.0", buf_size);
        return;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        strncpy(buf, "255.255.255.0", buf_size);
        return;
    }
    esp_ip4addr_ntoa(&ip_info.netmask, buf, buf_size);
}

void platform_get_hostname(char *buf, size_t buf_size)
{
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif) {
        strncpy(buf, "bitaxe-gateway", buf_size);
        return;
    }
    const char *hostname = NULL;
    if (esp_netif_get_hostname(netif, &hostname) != ESP_OK || !hostname) {
        strncpy(buf, "bitaxe-gateway", buf_size);
        return;
    }
    strncpy(buf, hostname, buf_size - 1);
    buf[buf_size - 1] = '\0';
}

// ── ARP ──────────────────────────────────────────────────────────────────────

#include "lwip/etharp.h"
#include "lwip/netif.h"

bool platform_arp_mac_to_ip(const char *mac, char *ip_buf, size_t ip_buf_size)
{
    /* Parse target MAC */
    unsigned int m[6];
    if (sscanf(mac, "%x:%x:%x:%x:%x:%x",
               &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
        return false;

    /* Iterate all ARP table slots — use continue (not break) on empty slots
     * because the table can have gaps between valid entries. */
    ip4_addr_t     *ip_ret;
    struct netif   *entry_netif;
    struct eth_addr *eth_ret;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (etharp_get_entry(i, &ip_ret, &entry_netif, &eth_ret) == 0) continue;
        if (eth_ret->addr[0] == (uint8_t)m[0] &&
            eth_ret->addr[1] == (uint8_t)m[1] &&
            eth_ret->addr[2] == (uint8_t)m[2] &&
            eth_ret->addr[3] == (uint8_t)m[3] &&
            eth_ret->addr[4] == (uint8_t)m[4] &&
            eth_ret->addr[5] == (uint8_t)m[5]) {
            esp_ip4addr_ntoa((const esp_ip4_addr_t *)ip_ret, ip_buf, (int)ip_buf_size);
            return true;
        }
    }
    return false;
}

bool platform_arp_ip_to_mac(const char *ip, char *mac_buf, size_t mac_buf_size)
{
    ip4_addr_t target;
    if (!ip4addr_aton(ip, &target)) return false;

    ip4_addr_t     *ip_ret;
    struct netif   *entry_netif;
    struct eth_addr *eth_ret;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (etharp_get_entry(i, &ip_ret, &entry_netif, &eth_ret) == 0) continue;
        if (ip4_addr_eq(ip_ret, &target)) {
            snprintf(mac_buf, mac_buf_size,
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                     eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
            return true;
        }
    }
    return false;
}

// ── Memory ───────────────────────────────────────────────────────────────────

void *platform_malloc_large(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

// ── WebSocket ────────────────────────────────────────────────────────────────

#define WS_MSG_QUEUE_SIZE 32
#define WS_MSG_MAX_LEN    8192

typedef enum {
    WS_EVT_CONNECTED,
    WS_EVT_DISCONNECTED,
    WS_EVT_MESSAGE,
} ws_evt_type_t;

typedef struct {
    ws_evt_type_t type;
    char         *data;  // heap-allocated; non-NULL only for WS_EVT_MESSAGE
    int           len;
} ws_evt_t;

static const char               *TAG_WS = "gw_ws";
static esp_websocket_client_handle_t s_ws_client   = NULL;
static volatile bool                 s_ws_connected = false;
static QueueHandle_t                 s_ws_evt_queue = NULL;

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    if (!s_ws_evt_queue) return;

    ws_evt_t evt = { 0 };

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_ws_connected = true;
        evt.type = WS_EVT_CONNECTED;
        xQueueSend(s_ws_evt_queue, &evt, 0);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
        s_ws_connected = false;
        evt.type = WS_EVT_DISCONNECTED;
        xQueueSend(s_ws_evt_queue, &evt, 0);
        break;

    case WEBSOCKET_EVENT_DATA: {
        esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;
        if (d->op_code == 0x01 && d->data_len > 0 && d->data_ptr) {
            /* Respond to PING immediately in the WS task — do NOT enqueue.
             * The main loop may be blocked executing a command, and the server
             * will close the connection if PONG doesn't arrive within ~5 s. */
            if (d->data_len == 4 && memcmp(d->data_ptr, "PING", 4) == 0) {
                ESP_LOGI(TAG_WS, "PING → PONG (inline)");
                platform_ws_send("PONG", 4);
                break;
            }

            int copy_len = d->data_len;
            if (copy_len > WS_MSG_MAX_LEN) copy_len = WS_MSG_MAX_LEN;

            char *copy = heap_caps_malloc(copy_len + 1, MALLOC_CAP_SPIRAM);
            if (!copy) copy = malloc(copy_len + 1);
            if (copy) {
                memcpy(copy, d->data_ptr, copy_len);
                copy[copy_len] = '\0';

                ESP_LOGI(TAG_WS, "RX (%d): %.*s", copy_len, copy_len, copy);
                evt.type = WS_EVT_MESSAGE;
                evt.data = copy;
                evt.len  = copy_len;
                if (xQueueSend(s_ws_evt_queue, &evt, 0) != pdTRUE) {
                    ESP_LOGW(TAG_WS, "Event queue full, dropping message");
                    free(copy);
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

void platform_ws_connect(const char *url)
{
    if (!s_ws_evt_queue) {
        s_ws_evt_queue = xQueueCreate(WS_MSG_QUEUE_SIZE, sizeof(ws_evt_t));
        if (!s_ws_evt_queue) {
            ESP_LOGE(TAG_WS, "Failed to create event queue");
            return;
        }
    }

    esp_websocket_client_config_t cfg = {
        .uri                 = url,
        .reconnect_timeout_ms = 0,      // outer loop retries
        .network_timeout_ms  = 10000,
        .ping_interval_sec   = 30,
        .buffer_size         = 8192,
        .crt_bundle_attach   = esp_crt_bundle_attach,
        .headers             = "ngrok-skip-browser-warning: true\r\n",
    };

    s_ws_client = esp_websocket_client_init(&cfg);
    if (!s_ws_client) {
        ESP_LOGE(TAG_WS, "Failed to init WebSocket client");
        return;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);

    esp_err_t err = esp_websocket_client_start(s_ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to start WebSocket client: %d", err);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
}

void platform_ws_disconnect(void)
{
    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
    s_ws_connected = false;

    // Drain leftover queued events and free any message copies
    if (s_ws_evt_queue) {
        ws_evt_t evt;
        while (xQueueReceive(s_ws_evt_queue, &evt, 0) == pdTRUE) {
            if (evt.data) free(evt.data);
        }
    }
}

bool platform_ws_is_connected(void)
{
    return s_ws_connected &&
           s_ws_client &&
           esp_websocket_client_is_connected(s_ws_client);
}

void platform_ws_send(const char *data, int len)
{
    if (s_ws_client && s_ws_connected) {
        ESP_LOGI(TAG_WS, "TX (%d): %.*s", len, len, data);
        esp_websocket_client_send_text(s_ws_client, data, len, pdMS_TO_TICKS(5000));
    }
}

void platform_ws_service(void)
{
    if (!s_ws_evt_queue) return;

    ws_evt_t evt;
    while (xQueueReceive(s_ws_evt_queue, &evt, 0) == pdTRUE) {
        switch (evt.type) {
        case WS_EVT_CONNECTED:
            gateway_core_on_ws_connected();
            break;
        case WS_EVT_DISCONNECTED:
            gateway_core_on_ws_disconnected();
            break;
        case WS_EVT_MESSAGE:
            if (evt.data) {
                gateway_core_on_message(evt.data, evt.len);
                free(evt.data);
            }
            break;
        }
    }
}

// ── HTTP ─────────────────────────────────────────────────────────────────────

#define HTTP_RESP_BUF_CAP 8192

typedef struct {
    char *buf;
    int   len;
    int   cap;
} http_resp_t;

static esp_err_t http_data_handler(esp_http_client_event_t *evt)
{
    http_resp_t *r = (http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && r) {
        if (r->len + evt->data_len < r->cap) {
            memcpy(r->buf + r->len, evt->data, evt->data_len);
            r->len += evt->data_len;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

int platform_http_request(const char *url,
                           const char *method,
                           const char *body,
                           char *resp_buf, int buf_size,
                           int timeout_ms)
{
    if (resp_buf && buf_size > 0) resp_buf[0] = '\0';

    http_resp_t resp = {
        .buf = resp_buf,
        .len = 0,
        .cap = buf_size,
    };

    esp_http_client_method_t esp_method = HTTP_METHOD_GET;
    if      (method && strcmp(method, "POST")  == 0) esp_method = HTTP_METHOD_POST;
    else if (method && strcmp(method, "PATCH") == 0) esp_method = HTTP_METHOD_PATCH;
    else if (method && strcmp(method, "PUT")   == 0) esp_method = HTTP_METHOD_PUT;

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = esp_method,
        .timeout_ms     = timeout_ms,
        .event_handler  = http_data_handler,
        .user_data      = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;

    if (body && (esp_method == HTTP_METHOD_POST ||
                 esp_method == HTTP_METHOD_PATCH ||
                 esp_method == HTTP_METHOD_PUT)) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = -1;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }

    esp_http_client_cleanup(client);
    return status;
}

// ── TCP ──────────────────────────────────────────────────────────────────────

bool platform_tcp_send_recv(const char *ip, int port,
                             const char *send_buf,
                             char *recv_buf, int recv_size,
                             int timeout_ms)
{
    struct sockaddr_in dest = { 0 };
    dest.sin_family = AF_INET;
    dest.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &dest.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    if (tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_sec = 1;

    /* Non-blocking connect so timeout_ms applies to the connect as well. */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    connect(sock, (struct sockaddr *)&dest, sizeof(dest)); /* EINPROGRESS expected */

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    if (select(sock + 1, NULL, &wfds, NULL, &tv) != 1) {
        close(sock);
        return false;
    }
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) {
        close(sock);
        return false;
    }

    /* Restore blocking mode for send/recv, reuse the same timeout. */
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int sent = send(sock, send_buf, strlen(send_buf), 0);
    if (sent < 0) {
        close(sock);
        return false;
    }

    int total = 0;
    while (total < recv_size - 1) {
        int r = recv(sock, recv_buf + total, recv_size - 1 - total, 0);
        if (r <= 0) break;
        total += r;
    }
    recv_buf[total] = '\0';
    close(sock);
    return total > 0;
}

// ── TCP probe ─────────────────────────────────────────────────────────────────

bool platform_tcp_probe(const char *ip, int port, int timeout_ms)
{
    struct sockaddr_in dest = { 0 };
    dest.sin_family = AF_INET;
    dest.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1) return false;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    /* Non-blocking connect + select so timeout_ms is honoured precisely. */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    connect(sock, (struct sockaddr *)&dest, sizeof(dest)); /* EINPROGRESS expected */

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    if (tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_sec = 1;

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    bool connected = false;
    if (select(sock + 1, NULL, &wfds, NULL, &tv) == 1) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
        connected = (err == 0);
    }
    close(sock);
    return connected;
}
