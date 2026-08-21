#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "stratum_task.h"
#include "stratum_v1_client.h"
#include "stratum_v2_client.h"
#include "stratum_api.h"
#include "connect.h"
#include "system.h"
#include "work_queue.h"
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#include "esp_transport_ssl.h"
#include "stratum_socket.h"

#define TAG "stratum_task"
#define TRANSPORT_TIMEOUT_MS 5000
#define MAX_RETRY_ATTEMPTS 3
#define HEARTBEAT_INTERVAL_MS 60000
#define INITIAL_HEARTBEAT_DELAY_MS 10000
#define RECOVERY_PROBE_INTERVAL_MS 30000
#define BUFFER_SIZE 1024

static GlobalState *s_global_state = NULL;
static volatile bool s_should_reconnect = false;
static TaskHandle_t s_heartbeat_task_handle = NULL;

void stratum_request_reconnect(void)
{
    s_should_reconnect = true;
    if (s_global_state) {
        taskENTER_CRITICAL(&s_global_state->stratum_mux);
        esp_transport_handle_t transport = s_global_state->transport;
        taskEXIT_CRITICAL(&s_global_state->stratum_mux);
        if (transport) {
            int sock = esp_transport_get_socket(transport);
            if (sock >= 0) {
                shutdown(sock, SHUT_RDWR);
            }
        }
    }
}

static bool probe_pool_sv2(const char *url, uint16_t port)
{
    if (url == NULL || url[0] == '\0' || port == 0) return false;

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(url, port, &conn_info) != ESP_OK) {
        return false;
    }

    esp_transport_handle_t probe = esp_transport_tcp_init();
    if (!probe) return false;

    esp_err_t err = esp_transport_connect(probe, conn_info.host_ip, port, TRANSPORT_TIMEOUT_MS);
    esp_transport_close(probe);
    esp_transport_destroy(probe);

    return (err == ESP_OK);
}

static bool probe_pool_v1(GlobalState *gs, const char *url, uint16_t port,
                          tls_mode tls, char *cert, const char *user, const char *pass)
{
    if (url == NULL || url[0] == '\0' || port == 0) return false;

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(url, port, &conn_info) != ESP_OK) {
        return false;
    }

    esp_transport_handle_t transport = STRATUM_V1_transport_init(tls, cert);
    if (!transport) return false;

    if (tls != DISABLED) {
        esp_transport_ssl_set_common_name(transport, url);
    }

    esp_err_t err = esp_transport_connect(transport, conn_info.host_ip, port, TRANSPORT_TIMEOUT_MS);
    if (err != ESP_OK) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    int send_uid = 1;
    STRATUM_V1_subscribe(transport, send_uid++, gs->DEVICE_CONFIG.family.asic.name);

    char recv_buffer[BUFFER_SIZE];
    memset(recv_buffer, 0, BUFFER_SIZE);
    int bytes_received = esp_transport_read(transport, recv_buffer, BUFFER_SIZE - 1, TRANSPORT_TIMEOUT_MS);

    esp_transport_close(transport);
    esp_transport_destroy(transport);

    return (bytes_received > 0 && (strstr(recv_buffer, "result") != NULL ||
                                   strstr(recv_buffer, "id") != NULL ||
                                   strstr(recv_buffer, "mining") != NULL));
}

bool stratum_probe_pool(GlobalState *gs, uint16_t pool_idx)
{
    PoolConfig *pool = &gs->SYSTEM_MODULE.pools[pool_idx];
    if (!pool->url || pool->url[0] == '\0' || pool->port == 0) return false;

    if (pool->protocol == STRATUM_PROTOCOL_V2) {
        return probe_pool_sv2(pool->url, pool->port);
    }
    return probe_pool_v1(gs, pool->url, pool->port, pool->tls, pool->cert, pool->user, pool->pass);
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

static void stratum_heartbeat_task(void *pvParameters)
{
    GlobalState *gs = (GlobalState *)pvParameters;

    while (1) {
        // Wait for periodic heartbeat or immediate wake-up via stratum_trigger_heartbeat_check()
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));

        if (!gs) continue;

        if (gs->SYSTEM_MODULE.is_using_fallback &&
            !gs->SYSTEM_MODULE.use_fallback_stratum &&
            wifi_is_connected()) {

            uint16_t prim_idx = gs->SYSTEM_MODULE.primary_pool_index;
            ESP_LOGD(TAG, "Heartbeat: probing primary pool %u...", prim_idx);
            if (stratum_probe_pool(gs, prim_idx)) {
                ESP_LOGI(TAG, "Primary pool %u is back online! Switching from fallback.", prim_idx);
                gs->SYSTEM_MODULE.is_using_fallback = false;
                stratum_request_reconnect();
            }
        }
    }
}

void stratum_trigger_heartbeat_check(void)
{
    if (s_heartbeat_task_handle) {
        xTaskNotifyGive(s_heartbeat_task_handle);
    }
}

static uint16_t s_running_pool_idx = 0;

void stratum_notify_pool_modified(GlobalState *gs, uint16_t pool_idx)
{
    if (!gs) return;

    uint16_t prim_idx = gs->SYSTEM_MODULE.primary_pool_index;

    if (pool_idx == s_running_pool_idx) {
        stratum_request_reconnect();
    } else if (pool_idx == prim_idx && gs->SYSTEM_MODULE.is_using_fallback && !gs->SYSTEM_MODULE.use_fallback_stratum) {
        stratum_trigger_heartbeat_check();
    }
}

void stratum_notify_pool_selection_changed(GlobalState *gs)
{
    if (!gs) return;

    uint16_t prim_idx = gs->SYSTEM_MODULE.primary_pool_index;
    uint16_t sec_idx = gs->SYSTEM_MODULE.secondary_pool_index;
    uint16_t target_idx = gs->SYSTEM_MODULE.is_using_fallback ? sec_idx : prim_idx;

    if (target_idx != s_running_pool_idx) {
        stratum_request_reconnect();
    }
}

void stratum_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    s_global_state = GLOBAL_STATE;

    int consecutive_pool_failures = 0;
    int retry_attempts = 0;

    ESP_LOGI(TAG, "Unified Stratum Task started");

    // Periodic heartbeat task to probe primary pool while running fallback
    if (xTaskCreateWithCaps(stratum_heartbeat_task, "stratum_hb", 8192, (void *)GLOBAL_STATE, 2, &s_heartbeat_task_handle, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stratum heartbeat task");
    }

    while (1) {
        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, waiting...");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        uint16_t prim_idx = GLOBAL_STATE->SYSTEM_MODULE.primary_pool_index;
        uint16_t sec_idx = GLOBAL_STATE->SYSTEM_MODULE.secondary_pool_index;
        bool has_fallback = (GLOBAL_STATE->SYSTEM_MODULE.pools[sec_idx].url != NULL &&
                             GLOBAL_STATE->SYSTEM_MODULE.pools[sec_idx].url[0] != '\0');

        int threshold = has_fallback ? 2 : 1;

        if (s_should_reconnect) {
            consecutive_pool_failures = 0;
            retry_attempts = 0;
            s_should_reconnect = false;
        }

        if (consecutive_pool_failures == 0) {
            if (GLOBAL_STATE->SYSTEM_MODULE.use_fallback_stratum) {
                GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = has_fallback;
            }
        }

        // Paused state when all configured pools have exhausted retries
        if (consecutive_pool_failures >= threshold) {
            GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = true;
            ESP_LOGW(TAG, "All configured pools unreachable, pausing mining to conserve power.");
            vTaskDelay(pdMS_TO_TICKS(RECOVERY_PROBE_INTERVAL_MS));

            if (!wifi_is_connected()) continue;

            bool prefer_fallback = GLOBAL_STATE->SYSTEM_MODULE.use_fallback_stratum && has_fallback;
            uint16_t first_idx = prefer_fallback ? sec_idx : prim_idx;
            uint16_t second_idx = prefer_fallback ? prim_idx : sec_idx;

            if (stratum_probe_pool(GLOBAL_STATE, first_idx)) {
                ESP_LOGI(TAG, "Pool %u reachable, resuming mining", first_idx);
                GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = false;
                GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = (first_idx == sec_idx);
                consecutive_pool_failures = 0;
                retry_attempts = 0;
                queue_clear(&GLOBAL_STATE->stratum_queue);
                reset_share_stats(GLOBAL_STATE);
            } else if (has_fallback && stratum_probe_pool(GLOBAL_STATE, second_idx)) {
                ESP_LOGI(TAG, "Pool %u reachable, resuming mining", second_idx);
                GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = false;
                GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = (second_idx == sec_idx);
                consecutive_pool_failures = 0;
                retry_attempts = 0;
                queue_clear(&GLOBAL_STATE->stratum_queue);
                reset_share_stats(GLOBAL_STATE);
            }
            continue;
        }

        uint16_t active_idx = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? sec_idx : prim_idx;
        stratum_protocol_t protocol = GLOBAL_STATE->SYSTEM_MODULE.pools[active_idx].protocol;

        s_running_pool_idx = active_idx;
        s_should_reconnect = false;
        esp_err_t err;

        if (protocol == STRATUM_PROTOCOL_V2) {
            err = stratum_v2_run(GLOBAL_STATE, active_idx, &s_should_reconnect);
        } else {
            err = stratum_v1_run(GLOBAL_STATE, active_idx, &s_should_reconnect);
        }

        if (err == ESP_OK || s_should_reconnect) {
            // Clean disconnect, mode switch, or reconnect requested
            consecutive_pool_failures = 0;
            retry_attempts = 0;
            GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = false;
            queue_clear(&GLOBAL_STATE->stratum_queue);
            reset_share_stats(GLOBAL_STATE);
            s_should_reconnect = false;
        } else {
            retry_attempts++;
            ESP_LOGW(TAG, "Pool %u (%s) connection failed (attempt %d/%d)",
                     active_idx, GLOBAL_STATE->SYSTEM_MODULE.pools[active_idx].url,
                     retry_attempts, MAX_RETRY_ATTEMPTS);

            if (retry_attempts >= MAX_RETRY_ATTEMPTS) {
                consecutive_pool_failures++;
                retry_attempts = 0;

                if (has_fallback) {
                    GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = !GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback;
                    ESP_LOGI(TAG, "Switching to %s pool (%s)",
                             GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? "fallback" : "primary",
                             GLOBAL_STATE->SYSTEM_MODULE.pools[GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? sec_idx : prim_idx].url);
                    queue_clear(&GLOBAL_STATE->stratum_queue);
                    reset_share_stats(GLOBAL_STATE);
                }
            }
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
}

int stratum_submit_share(GlobalState *GLOBAL_STATE, const bm_job *active_job,
                         uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us)
{
    if (!GLOBAL_STATE || !active_job) {
        return -1;
    }

    uint16_t active_pool_idx = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback
        ? GLOBAL_STATE->SYSTEM_MODULE.secondary_pool_index
        : GLOBAL_STATE->SYSTEM_MODULE.primary_pool_index;

    if (active_job->pool_id != (uint8_t)active_pool_idx) {
        ESP_LOGW(TAG, "Dropping share for stale pool index %u (active pool is %u)",
                 active_job->pool_id, active_pool_idx);
        return -1;
    }

    int ret;
    if (active_job->job_type == JOB_TYPE_SV2_STANDARD || active_job->job_type == JOB_TYPE_SV2_EXTENDED) {
        ret = stratum_v2_submit_share(GLOBAL_STATE, active_job, nonce, rolled_version, sent_time_us);
    } else {
        ret = stratum_v1_submit_share(GLOBAL_STATE, active_job, nonce, rolled_version, sent_time_us);
    }

    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to submit share to socket (ret: %d, errno %d: %s)", ret, errno, strerror(errno));
        // stratum_task recv loop will detect a broken connection on its next read and handle reconnection
    }
    return ret;
}
