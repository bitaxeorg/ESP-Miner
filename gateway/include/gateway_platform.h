#pragma once

/**
 * Gateway Platform HAL
 *
 * Every function declared here must be implemented once per target:
 *   gateway/esp32/gateway_platform_esp32.c   — ESP32 / ESP-IDF
 *   gateway/macos/gateway_platform_macos.c   — macOS / POSIX
 *
 * gateway_core.c calls only these symbols and standard C —
 * no platform SDKs leak into core code.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "gateway_types.h"

// ── Logging ────────────────────────────────────────────────────────────────

#define GW_LOG_ERROR 0
#define GW_LOG_WARN  1
#define GW_LOG_INFO  2
#define GW_LOG_DEBUG 3

void platform_log(int level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define GW_LOGE(tag, ...) platform_log(GW_LOG_ERROR, tag, __VA_ARGS__)
#define GW_LOGW(tag, ...) platform_log(GW_LOG_WARN,  tag, __VA_ARGS__)
#define GW_LOGI(tag, ...) platform_log(GW_LOG_INFO,  tag, __VA_ARGS__)
#define GW_LOGD(tag, ...) platform_log(GW_LOG_DEBUG, tag, __VA_ARGS__)

// ── Timing & system ────────────────────────────────────────────────────────

/** Monotonic millisecond clock (always increases, never wraps in practice). */
int64_t platform_time_ms(void);

/** Block the calling thread for the given number of milliseconds. */
void platform_delay_ms(uint32_t ms);

/** Approximate free heap in bytes (diagnostic only). */
size_t platform_get_free_heap(void);

/** Trigger a system reboot. Does not return. */
void platform_reboot(void);

// ── Network identity ───────────────────────────────────────────────────────

/** Fill buf with the primary MAC address string, e.g. "AA:BB:CC:DD:EE:FF". */
void platform_get_mac_str(char *buf, size_t buf_size);

/** Fill buf with the local IP address string, e.g. "192.168.1.100". */
void platform_get_local_ip(char *buf, size_t buf_size);

/** Fill buf with the local netmask string, e.g. "255.255.255.0". */
void platform_get_netmask(char *buf, size_t buf_size);

/** Fill buf with the hostname, e.g. "bitaxe-gateway". */
void platform_get_hostname(char *buf, size_t buf_size);

// ── Memory ─────────────────────────────────────────────────────────────────

/**
 * Allocate a large buffer.
 * On ESP32 this targets SPIRAM; elsewhere it falls back to malloc().
 * Caller must free() the result.
 */
void *platform_malloc_large(size_t size);

// ── WebSocket ──────────────────────────────────────────────────────────────

/**
 * Initiate an async WebSocket connection to url.
 * The platform internally starts whatever service mechanism it needs
 * (FreeRTOS task, background thread, etc.) and will call
 * gateway_core_on_ws_connected / _disconnected / (enqueue messages)
 * as events arrive.
 */
void platform_ws_connect(const char *url);

/** Gracefully close the current WebSocket connection and stop its service. */
void platform_ws_disconnect(void);

/** Returns true while the transport considers itself connected. */
bool platform_ws_is_connected(void);

/**
 * Send a text frame.  Must only be called from the gateway core's main loop
 * (not from an event/transport thread).
 */
void platform_ws_send(const char *data, int len);

/**
 * Drain the incoming message queue and dispatch each message by calling
 * gateway_core_on_message(data, len).
 * Called from the gateway core's main loop (100 ms cadence).
 */
void platform_ws_service(void);

// ── Callbacks implemented by gateway_core.c, called by the platform ────────

/** Called by the platform when the WebSocket transport connects. */
void gateway_core_on_ws_connected(void);

/** Called by the platform when the WebSocket transport disconnects. */
void gateway_core_on_ws_disconnected(void);

/**
 * Called by the platform (from its service/drain loop) for each received
 * text frame.  data is a null-terminated copy owned by the caller.
 */
void gateway_core_on_message(const char *data, int len);

// ── HTTP ───────────────────────────────────────────────────────────────────

/**
 * Perform a synchronous HTTP request.
 *
 * Returns the HTTP status code (e.g. 200) on success, or -1 on error.
 * resp_buf is null-terminated on success; its length is not returned but
 * callers may use strlen(resp_buf).
 *
 * method: "GET", "POST", "PATCH"
 * body:   NULL for requests with no body
 */
int platform_http_request(const char *url,
                           const char *method,
                           const char *body,
                           char *resp_buf, int buf_size,
                           int timeout_ms);

// ── TCP ────────────────────────────────────────────────────────────────────

/**
 * Open a TCP connection, send send_buf, read the response into recv_buf,
 * then close.  Returns true on success.
 * timeout_ms controls both connect and send/recv timeouts.
 */
bool platform_tcp_send_recv(const char *ip, int port,
                             const char *send_buf,
                             char *recv_buf, int recv_size,
                             int timeout_ms);

/**
 * Attempt a TCP connect to ip:port and return true if it succeeds within
 * timeout_ms milliseconds (used for SCAN_IP_RANGE port probing).
 * The connection is closed immediately after the test.
 */
bool platform_tcp_probe(const char *ip, int port, int timeout_ms);

// ── ARP ────────────────────────────────────────────────────────────────────

/**
 * Look up the IP address for a given MAC in the local ARP table.
 * mac: canonical colon-separated hex string, any case ("AA:BB:CC:DD:EE:FF").
 * Returns true and fills ip_buf on success; returns false if not found.
 */
bool platform_arp_mac_to_ip(const char *mac, char *ip_buf, size_t ip_buf_size);

/**
 * Look up the MAC address for a given IP in the local ARP table.
 * Returns true and fills mac_buf (uppercase colon form) on success.
 */
bool platform_arp_ip_to_mac(const char *ip, char *mac_buf, size_t mac_buf_size);
