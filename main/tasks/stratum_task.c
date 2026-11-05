#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "stratum_task.h"
#include "stratum_driver.h"
#include "stratum_v1_client.h"
#include "stratum_v2_client.h"
#include "connect.h"
#include "system.h"
#include <string.h>

#define TAG "stratum_task"
#define MAX_RETRY_ATTEMPTS 3
#define RECOVERY_PROBE_INTERVAL_MS 30000

static TaskHandle_t s_primary_task_handle = NULL;
static TaskHandle_t s_fallback_task_handle = NULL;
static volatile bool s_should_reconnect = false;

void stratum_abort_pool(uint16_t pool_idx)
{
    if (pool_idx >= MAX_POOLS) return;
    const stratum_driver_t *v1 = stratum_get_driver(STRATUM_PROTOCOL_V1);
    const stratum_driver_t *v2 = stratum_get_driver(STRATUM_PROTOCOL_V2);
    if (v1 && v1->abort_connection) v1->abort_connection(pool_idx);
    if (v2 && v2->abort_connection) v2->abort_connection(pool_idx);
    if (s_fallback_task_handle) {
        xTaskNotifyGive(s_fallback_task_handle);
    }
}

void stratum_abort_connection(void)
{
    s_should_reconnect = true;
    for (uint16_t i = 0; i < MAX_POOLS; i++) {
        stratum_abort_pool(i);
    }
}

bool stratum_reconnect_requested(void)
{
    return s_should_reconnect;
}

void stratum_set_pool_error(GlobalState *gs, uint16_t pool_idx, const char *err_msg)
{
    if (!gs) return;
    if (gs->SYSTEM_MODULE.is_using_fallback &&
        pool_idx == gs->SYSTEM_MODULE.primary_pool_index) {
        strlcpy(gs->SYSTEM_MODULE.primary_pool_error, err_msg,
                sizeof(gs->SYSTEM_MODULE.primary_pool_error));
    } else {
        strlcpy(gs->SYSTEM_MODULE.pool_connection_info, err_msg,
                sizeof(gs->SYSTEM_MODULE.pool_connection_info));
    }
}

void stratum_check_primary_handover(GlobalState *gs, uint16_t pool_idx)
{
    if (!gs) return;
    if (pool_idx == gs->SYSTEM_MODULE.primary_pool_index &&
        gs->SYSTEM_MODULE.is_using_fallback &&
        !gs->SYSTEM_MODULE.use_fallback_stratum) {
        ESP_LOGI(TAG, "Primary pool %u received work! Switching from fallback.", pool_idx);
        gs->SYSTEM_MODULE.is_using_fallback = false;
        gs->SYSTEM_MODULE.primary_pool_error[0] = '\0';
        uint16_t sec_idx = gs->SYSTEM_MODULE.secondary_pool_index;
        stratum_abort_pool(sec_idx);
    }
}

static esp_err_t run_pool_session(GlobalState *gs, uint16_t pool_idx)
{
    if (!gs || pool_idx >= MAX_POOLS) {
        return ESP_ERR_INVALID_ARG;
    }
    const PoolConfig *pool = &gs->SYSTEM_MODULE.pools[pool_idx];
    if (!pool->url || pool->url[0] == '\0' || pool->port == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const stratum_driver_t *driver = stratum_get_driver(pool->protocol);
    if (!driver || !driver->run) {
        ESP_LOGE(TAG, "No driver found for protocol %d", pool->protocol);
        return ESP_ERR_NOT_FOUND;
    }

    s_should_reconnect = false;
    return driver->run(gs, pool_idx);
}

static void start_fallback_if_needed(GlobalState *gs)
{
    uint16_t sec_idx = gs->SYSTEM_MODULE.secondary_pool_index;
    const PoolConfig *pool = &gs->SYSTEM_MODULE.pools[sec_idx];
    bool has_fallback = (pool->url != NULL && pool->url[0] != '\0' && pool->port != 0);

    if (!has_fallback) {
        gs->SYSTEM_MODULE.pools_unavailable = true;
        return;
    }

    if (s_fallback_task_handle == NULL) {
        ESP_LOGI(TAG, "Starting stratum_task for fallback pool %u (%s)", sec_idx, pool->url);
        if (xTaskCreateWithCaps(stratum_task, "stratum_fb", 16384, (void *)gs, 16, &s_fallback_task_handle, MALLOC_CAP_SPIRAM) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start fallback stratum task");
        }
    }
}

void stratum_notify_pool_modified(GlobalState *gs, uint16_t pool_idx)
{
    if (!gs || pool_idx >= MAX_POOLS) return;

    gs->SYSTEM_MODULE.primary_pool_error[0] = '\0';
    gs->SYSTEM_MODULE.pool_banner[0] = '\0';

    stratum_abort_pool(pool_idx);

    if (pool_idx == gs->SYSTEM_MODULE.primary_pool_index &&
        gs->SYSTEM_MODULE.is_using_fallback &&
        !gs->SYSTEM_MODULE.use_fallback_stratum) {
        if (s_primary_task_handle) {
            xTaskNotifyGive(s_primary_task_handle);
        }
    }
}

void stratum_notify_pool_selection_changed(GlobalState *gs)
{
    if (!gs) return;

    gs->SYSTEM_MODULE.primary_pool_error[0] = '\0';
    gs->SYSTEM_MODULE.pool_banner[0] = '\0';

    stratum_abort_connection();
    if (s_primary_task_handle) {
        xTaskNotifyGive(s_primary_task_handle);
    }
}

void stratum_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    bool is_primary;

    if (s_primary_task_handle == NULL || xTaskGetCurrentTaskHandle() == s_primary_task_handle) {
        s_primary_task_handle = xTaskGetCurrentTaskHandle();
        is_primary = true;
        ESP_LOGI(TAG, "Primary stratum task started");
    } else {
        s_fallback_task_handle = xTaskGetCurrentTaskHandle();
        is_primary = false;
        ESP_LOGI(TAG, "Fallback stratum task started");
    }

    int retry_attempts = 0;

    while (1) {
        // Fallback task exits when primary has recovered and fallback is not forced
        if (!is_primary && !GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback && !GLOBAL_STATE->SYSTEM_MODULE.use_fallback_stratum) {
            break;
        }

        if (GLOBAL_STATE->SYSTEM_MODULE.mining_paused || GLOBAL_STATE->SYSTEM_MODULE.hardware_fault) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, waiting...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // If force fallback is toggled on, Primary steps down and sleeps
        if (is_primary && GLOBAL_STATE->SYSTEM_MODULE.use_fallback_stratum) {
            stratum_abort_pool(GLOBAL_STATE->SYSTEM_MODULE.primary_pool_index);
            GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = true;
            start_fallback_if_needed(GLOBAL_STATE);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
            continue;
        }

        uint16_t pool_idx = is_primary ? GLOBAL_STATE->SYSTEM_MODULE.primary_pool_index
                                       : GLOBAL_STATE->SYSTEM_MODULE.secondary_pool_index;

        const PoolConfig *pool = &GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx];
        if (!pool->url || pool->url[0] == '\0' || pool->port == 0) {
            ESP_LOGW(TAG, "%s pool %u not configured", is_primary ? "Primary" : "Fallback", pool_idx);
            GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = true;
            if (!is_primary) break;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "%s worker running pool %u (%s)", is_primary ? "Primary" : "Fallback", pool_idx, pool->url);
        esp_err_t err = run_pool_session(GLOBAL_STATE, pool_idx);

        // Check if fallback should exit after session unblocks
        if (!is_primary && !GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback && !GLOBAL_STATE->SYSTEM_MODULE.use_fallback_stratum) {
            break;
        }

        if (err == ESP_OK || GLOBAL_STATE->SYSTEM_MODULE.work_received > 0) {
            retry_attempts = 0;
            if (is_primary) {
                GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = false;
                GLOBAL_STATE->SYSTEM_MODULE.primary_pool_error[0] = '\0';
            }
            GLOBAL_STATE->SYSTEM_MODULE.work_received = 0;
            SYSTEM_reset_pool_session(GLOBAL_STATE);
        } else {
            if (is_primary && GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback) {
                ESP_LOGI(TAG, "Primary pool %u probe failed, waiting %d seconds...",
                         pool_idx, RECOVERY_PROBE_INTERVAL_MS / 1000);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RECOVERY_PROBE_INTERVAL_MS));
                continue;
            }

            retry_attempts++;
            ESP_LOGW(TAG, "%s pool %u (%s) failed (attempt %d/%d)",
                     is_primary ? "Primary" : "Fallback", pool_idx, pool->url, retry_attempts, MAX_RETRY_ATTEMPTS);

            if (retry_attempts >= MAX_RETRY_ATTEMPTS) {
                retry_attempts = 0;

                if (is_primary) {
                    strlcpy(GLOBAL_STATE->SYSTEM_MODULE.primary_pool_error,
                            GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                            sizeof(GLOBAL_STATE->SYSTEM_MODULE.primary_pool_error));

                    GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = true;
                    start_fallback_if_needed(GLOBAL_STATE);

                    ESP_LOGI(TAG, "Primary waiting %d seconds before retry...", RECOVERY_PROBE_INTERVAL_MS / 1000);
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RECOVERY_PROBE_INTERVAL_MS));
                } else {
                    GLOBAL_STATE->SYSTEM_MODULE.pools_unavailable = true;
                    ESP_LOGW(TAG, "Both primary and fallback pools unavailable");
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RECOVERY_PROBE_INTERVAL_MS));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
    }

    if (!is_primary) {
        ESP_LOGI(TAG, "Fallback stratum task exiting");
        s_fallback_task_handle = NULL;
        vTaskDelete(NULL);
    }
}

int stratum_submit_share(GlobalState *GLOBAL_STATE, const asic_job_t *active_job,
                         uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us)
{
    if (!GLOBAL_STATE || !active_job || active_job->pool_id >= MAX_POOLS) {
        return -1;
    }

    const stratum_driver_t *driver = stratum_get_driver_for_job(active_job);
    if (!driver || !driver->submit_share) {
        return -1;
    }

    int ret = driver->submit_share(GLOBAL_STATE, active_job, nonce, rolled_version, sent_time_us);

    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to submit share to socket for pool %u (ret: %d)",
                 active_job->pool_id, ret);
    }
    return ret;
}
