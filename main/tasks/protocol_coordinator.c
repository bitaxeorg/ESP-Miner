#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "protocol_coordinator.h"
#include "stratum_v1_task.h"
#include "stratum_v2_task.h"
#include "system.h"
#include "nvs_config.h"

#include <string.h>

// Internal coordinator states
typedef enum {
    COORD_STATE_IDLE = 0,
    COORD_STATE_RUNNING_PRIMARY,
    COORD_STATE_RUNNING_FALLBACK,
} coordinator_state_t;

// Internal event types
typedef enum {
    COORD_EVENT_PROTOCOL_FAILED = 0,
    COORD_EVENT_V1_TASK_EXITED,
    COORD_EVENT_V2_TASK_EXITED,
} coordinator_event_t;

#define TRANSPORT_TIMEOUT_MS 5000
#define HEARTBEAT_INTERVAL_MS 60000
#define INITIAL_HEARTBEAT_DELAY_MS 10000
#define BUFFER_SIZE 1024

static const char *TAG = "protocol_coordinator";

static GlobalState *s_global_state = NULL;
static coordinator_state_t s_state = COORD_STATE_IDLE;
static QueueHandle_t s_event_queue = NULL;
static volatile bool s_v1_should_shutdown = false;
static volatile bool s_v2_should_shutdown = false;

// Protocol tracking
static stratum_protocol_t s_primary_protocol;
static stratum_protocol_t s_fallback_protocol;
static stratum_protocol_t s_running_protocol;
static bool s_heartbeat_enabled = false;

// Primary pool info (saved at startup for heartbeat probing)
static const char *s_primary_url = NULL;
static uint16_t s_primary_port = 0;

void protocol_coordinator_init(GlobalState *gs)
{
    s_global_state = gs;
    s_event_queue = xQueueCreate(8, sizeof(coordinator_event_t));
    s_v1_should_shutdown = false;
    s_v2_should_shutdown = false;
    s_heartbeat_enabled = false;
}

void protocol_coordinator_notify_failure(void)
{
    coordinator_event_t evt = COORD_EVENT_PROTOCOL_FAILED;
    if (s_event_queue) {
        xQueueSend(s_event_queue, &evt, 0);
    }
}

bool protocol_coordinator_v1_should_shutdown(void)
{
    return s_v1_should_shutdown;
}

void protocol_coordinator_v1_exited(void)
{
    coordinator_event_t evt = COORD_EVENT_V1_TASK_EXITED;
    if (s_event_queue) {
        xQueueSend(s_event_queue, &evt, 0);
    }
}

bool protocol_coordinator_v2_should_shutdown(void)
{
    return s_v2_should_shutdown;
}

void protocol_coordinator_v2_exited(void)
{
    coordinator_event_t evt = COORD_EVENT_V2_TASK_EXITED;
    if (s_event_queue) {
        xQueueSend(s_event_queue, &evt, 0);
    }
}

static bool is_wifi_connected(void)
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

static void reset_share_stats(GlobalState *gs)
{
    for (int i = 0; i < gs->SYSTEM_MODULE.rejected_reason_stats_count; i++) {
        gs->SYSTEM_MODULE.rejected_reason_stats[i].count = 0;
        gs->SYSTEM_MODULE.rejected_reason_stats[i].message[0] = '\0';
    }
    gs->SYSTEM_MODULE.rejected_reason_stats_count = 0;
    gs->SYSTEM_MODULE.shares_accepted = 0;
    gs->SYSTEM_MODULE.shares_rejected = 0;
    gs->SYSTEM_MODULE.work_received = 0;
}

static bool has_fallback_pool(GlobalState *gs)
{
    return (gs->SYSTEM_MODULE.fallback_pool_url != NULL &&
            gs->SYSTEM_MODULE.fallback_pool_url[0] != '\0');
}

// Start the V1 stratum task (for primary V1 or fallback)
static void start_v1_task(GlobalState *gs)
{
    s_v1_should_shutdown = false;
    if (xTaskCreate(stratum_v1_task, "stratum v1", 8192, (void *)gs, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create V1 stratum task");
    }
}

// Start the V2 stratum task
static void start_v2_task(GlobalState *gs)
{
    s_v2_should_shutdown = false;
    if (xTaskCreate(stratum_v2_task, "stratum v2", 12288, (void *)gs, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create V2 stratum task");
    }
}

// Start a task for the given protocol
static void start_protocol_task(GlobalState *gs, stratum_protocol_t protocol)
{
    if (protocol == STRATUM_V2) {
        start_v2_task(gs);
    } else {
        start_v1_task(gs);
    }
}

// Tell the V1 task to shut down and wait for it to exit.
// Only closes the transport socket to unblock V1's recv — does NOT destroy it.
// The V1 task handles its own full cleanup (destroy, queue clear) on exit.
static void stop_v1_task(GlobalState *gs)
{
    s_v1_should_shutdown = true;

    // Close transport to unblock V1's blocked recv()
    if (gs->transport) {
        esp_transport_close(gs->transport);
    }

    coordinator_event_t evt;
    for (int i = 0; i < 100; i++) {
        if (xQueueReceive(s_event_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (evt == COORD_EVENT_V1_TASK_EXITED || evt == COORD_EVENT_PROTOCOL_FAILED) {
                ESP_LOGI(TAG, "V1 task exited cleanly");
                return;
            }
        }
    }
    ESP_LOGW(TAG, "V1 task did not exit within timeout");
}

// Tell the V2 task to shut down and wait for it to exit.
// Only closes the transport socket to unblock V2's recv — does NOT destroy it.
// The V2 task handles its own full cleanup (destroy, noise ctx, queue clear) on exit.
static void stop_v2_task(GlobalState *gs)
{
    s_v2_should_shutdown = true;

    // Close transport to unblock V2's blocked recv()
    if (gs->transport) {
        esp_transport_close(gs->transport);
    }

    coordinator_event_t evt;
    for (int i = 0; i < 100; i++) {
        if (xQueueReceive(s_event_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (evt == COORD_EVENT_V2_TASK_EXITED || evt == COORD_EVENT_PROTOCOL_FAILED) {
                ESP_LOGI(TAG, "V2 task exited cleanly");
                return;
            }
        }
    }
    ESP_LOGW(TAG, "V2 task did not exit within timeout");
}

// Stop the currently running protocol task
static void stop_running_task(GlobalState *gs)
{
    if (s_running_protocol == STRATUM_V2) {
        stop_v2_task(gs);
    } else {
        stop_v1_task(gs);
    }
}

// Probe whether the primary pool is reachable (TCP connect test for SV2)
static bool heartbeat_probe_sv2(void)
{
    esp_transport_handle_t probe = esp_transport_tcp_init();
    if (!probe) return false;

    esp_err_t err = esp_transport_connect(probe, s_primary_url, s_primary_port, TRANSPORT_TIMEOUT_MS);
    esp_transport_close(probe);
    esp_transport_destroy(probe);

    return (err == ESP_OK);
}

// Probe whether the primary pool is reachable (subscribe/authorize test for V1)
static bool heartbeat_probe_v1(GlobalState *gs)
{
    tls_mode tls = gs->SYSTEM_MODULE.pool_tls;
    char *cert = gs->SYSTEM_MODULE.pool_cert;
    esp_transport_handle_t transport = STRATUM_V1_transport_init(tls, cert);
    if (!transport) return false;

    esp_err_t err = esp_transport_connect(transport, s_primary_url, s_primary_port, TRANSPORT_TIMEOUT_MS);
    if (err != ESP_OK) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    int send_uid = 1;
    STRATUM_V1_subscribe(transport, send_uid++, gs->DEVICE_CONFIG.family.asic.name);
    STRATUM_V1_authorize(transport, send_uid++, gs->SYSTEM_MODULE.pool_user, gs->SYSTEM_MODULE.pool_pass);

    char recv_buffer[BUFFER_SIZE];
    memset(recv_buffer, 0, BUFFER_SIZE);
    int bytes_received = esp_transport_read(transport, recv_buffer, BUFFER_SIZE - 1, TRANSPORT_TIMEOUT_MS);

    esp_transport_close(transport);
    esp_transport_destroy(transport);

    return (bytes_received > 0 && strstr(recv_buffer, "mining.notify") != NULL);
}

// Switch from primary to fallback pool.
// The failed task has already exited (it sent PROTOCOL_FAILED then deleted itself).
static void switch_to_fallback(GlobalState *gs)
{
    queue_clear(&gs->stratum_queue);
    reset_share_stats(gs);

    gs->SYSTEM_MODULE.is_using_fallback = true;
    gs->stratum_protocol = s_fallback_protocol;
    s_running_protocol = s_fallback_protocol;
    s_state = COORD_STATE_RUNNING_FALLBACK;

    ESP_LOGI(TAG, "Switching to fallback pool (%s)",
             s_fallback_protocol == STRATUM_V2 ? "SV2" : "V1");

    start_protocol_task(gs, s_fallback_protocol);

    // Only enable heartbeat if this was an automatic failover (not user choice)
    s_heartbeat_enabled = !gs->SYSTEM_MODULE.use_fallback_stratum;
}

// Switch from fallback back to primary pool.
// Must stop the running fallback task first.
static void switch_to_primary(GlobalState *gs)
{
    ESP_LOGI(TAG, "Primary pool is back! Switching from fallback.");

    stop_running_task(gs);

    queue_clear(&gs->stratum_queue);
    reset_share_stats(gs);

    gs->SYSTEM_MODULE.is_using_fallback = false;
    gs->stratum_protocol = s_primary_protocol;
    s_running_protocol = s_primary_protocol;
    s_state = COORD_STATE_RUNNING_PRIMARY;

    start_protocol_task(gs, s_primary_protocol);

    s_heartbeat_enabled = false;
}

// Non-blocking heartbeat probe — called when the heartbeat timer expires
static void do_heartbeat_probe(GlobalState *gs)
{
    // Never auto-switch back if user explicitly chose fallback
    if (gs->SYSTEM_MODULE.use_fallback_stratum) {
        s_heartbeat_enabled = false;
        return;
    }

    if (!is_wifi_connected()) {
        return;
    }

    ESP_LOGD(TAG, "Heartbeat: probing primary pool %s:%d", s_primary_url, s_primary_port);

    bool primary_reachable;
    if (s_primary_protocol == STRATUM_V2) {
        primary_reachable = heartbeat_probe_sv2();
    } else {
        primary_reachable = heartbeat_probe_v1(gs);
    }

    if (primary_reachable) {
        switch_to_primary(gs);
    } else {
        ESP_LOGD(TAG, "Primary pool still unreachable");
    }
}

// Handle an event from the event queue
static void handle_event(GlobalState *gs, coordinator_event_t evt)
{
    switch (evt) {
        case COORD_EVENT_PROTOCOL_FAILED:
            ESP_LOGW(TAG, "Protocol failure reported (state=%d)", s_state);

            if (s_state == COORD_STATE_RUNNING_PRIMARY) {
                // Primary failed — switch to fallback if configured
                if (has_fallback_pool(gs)) {
                    switch_to_fallback(gs);
                } else {
                    // No fallback — restart the primary task after a delay
                    ESP_LOGW(TAG, "No fallback configured, restarting primary task");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    reset_share_stats(gs);
                    start_protocol_task(gs, s_primary_protocol);
                }
            } else if (s_state == COORD_STATE_RUNNING_FALLBACK) {
                // Fallback also failed — restart the fallback task after a delay
                ESP_LOGW(TAG, "Fallback pool also failed, restarting fallback task");
                vTaskDelay(pdMS_TO_TICKS(5000));
                reset_share_stats(gs);
                start_protocol_task(gs, s_running_protocol);
            }
            break;

        case COORD_EVENT_V1_TASK_EXITED:
        case COORD_EVENT_V2_TASK_EXITED:
            // These come from clean coordinator-requested shutdowns (via stop functions).
            // They're consumed by stop_v1_task/stop_v2_task during switch_to_primary.
            // If we receive one here unexpectedly, just log it.
            ESP_LOGI(TAG, "Task exited event received (evt=%d, state=%d)", evt, s_state);
            break;
    }
}

void protocol_coordinator_task(void *pvParameters)
{
    GlobalState *gs = (GlobalState *)pvParameters;

    s_primary_url = gs->SYSTEM_MODULE.pool_url;
    s_primary_port = gs->SYSTEM_MODULE.pool_port;
    s_primary_protocol = gs->stratum_protocol;
    s_fallback_protocol = gs->SYSTEM_MODULE.fallback_pool_protocol;

    // Start initial protocol task
    if (gs->SYSTEM_MODULE.is_using_fallback) {
        // User explicitly selected fallback — use fallback protocol
        gs->stratum_protocol = s_fallback_protocol;
        s_running_protocol = s_fallback_protocol;
        s_state = COORD_STATE_RUNNING_FALLBACK;
        start_protocol_task(gs, s_fallback_protocol);
        // User chose fallback, no heartbeat
        s_heartbeat_enabled = false;
    } else {
        s_running_protocol = s_primary_protocol;
        s_state = COORD_STATE_RUNNING_PRIMARY;
        start_protocol_task(gs, s_primary_protocol);
    }

    ESP_LOGI(TAG, "Protocol coordinator started (primary: %s, fallback: %s, state: %d)",
             s_primary_protocol == STRATUM_V2 ? "SV2" : "V1",
             s_fallback_protocol == STRATUM_V2 ? "SV2" : "V1",
             s_state);

    // Heartbeat initial delay state — give fallback connection time to establish
    // before probing primary pool
    bool heartbeat_initial_delay = false;
    int64_t heartbeat_delay_start = 0;

    // Main non-blocking event loop
    while (1) {
        coordinator_event_t evt;
        TickType_t wait = s_heartbeat_enabled
            ? pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)
            : portMAX_DELAY;

        bool was_heartbeat_enabled = s_heartbeat_enabled;

        if (xQueueReceive(s_event_queue, &evt, wait) == pdTRUE) {
            handle_event(gs, evt);

            // Detect heartbeat disabled→enabled transition, reset initial delay
            if (s_heartbeat_enabled && !was_heartbeat_enabled) {
                heartbeat_initial_delay = true;
                heartbeat_delay_start = esp_timer_get_time();
            }
        } else if (s_heartbeat_enabled) {
            // Timeout expired — time for a heartbeat probe
            if (heartbeat_initial_delay) {
                int64_t elapsed_ms = (esp_timer_get_time() - heartbeat_delay_start) / 1000;
                if (elapsed_ms < INITIAL_HEARTBEAT_DELAY_MS) {
                    continue;
                }
                heartbeat_initial_delay = false;
            }
            do_heartbeat_probe(gs);
        }
    }

    vTaskDelete(NULL);
}
