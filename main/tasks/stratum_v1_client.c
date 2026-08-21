#include "esp_log.h"
#include "esp_system.h"
#include "system.h"
#include "global_state.h"
#include <lwip/tcpip.h>
#include "stratum_v1_client.h"
#include "stratum_api.h"
#include "stratum_socket.h"
#include "connect.h"
#include "work_queue.h"
#include <esp_sntp.h>
#include "esp_timer.h"
#include "esp_transport.h"
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "coinbase_decoder.h"
#include "miner_job.h"
#include <esp_heap_caps.h>
#include "esp_transport_ssl.h"
#include "freertos/task.h"

#define MAX_EXTRANONCE_2_LEN 32
#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define MAX_ACTIVE_JOB_IDS 16

static const char *TAG = "stratum_v1";

static bool add_active_job_id(char active_job_ids[][MAX_JOB_ID_LEN], int *count, const char *job_id)
{
    for (int i = 0; i < *count; i++) {
        if (strncmp(active_job_ids[i], job_id, MAX_JOB_ID_LEN) == 0) {
            return false;
        }
    }
    if (*count < SV1_MAX_ACTIVE_JOB_IDS) {
        strlcpy(active_job_ids[*count], job_id, MAX_JOB_ID_LEN);
        (*count)++;
    } else {
        for (int i = 1; i < SV1_MAX_ACTIVE_JOB_IDS; i++) {
            memcpy(active_job_ids[i - 1], active_job_ids[i], MAX_JOB_ID_LEN);
        }
        strlcpy(active_job_ids[SV1_MAX_ACTIVE_JOB_IDS - 1], job_id, MAX_JOB_ID_LEN);
    }
    return true;
}

static void clear_active_job_ids(char active_job_ids[][MAX_JOB_ID_LEN], int *count)
{
    *count = 0;
}

static StratumApiV1Message stratum_api_v1_message = {};
static sv1_conn_t *s_v1_conn = NULL;

static int stratum_get_next_uid(GlobalState * GLOBAL_STATE)
{
    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    int uid = s_v1_conn ? s_v1_conn->send_uid++ : 1;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);
    return uid;
}

static void stratum_v1_reset_uid(GlobalState *GLOBAL_STATE)
{
    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    if (s_v1_conn) s_v1_conn->send_uid = 1;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);
}

int stratum_v1_submit_share(GlobalState *GLOBAL_STATE, const bm_job *active_job,
                            uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us)
{
    if (!GLOBAL_STATE || !active_job || !s_v1_conn) return -1;
    uint8_t pool_id = active_job->pool_id;
    char *user = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_id].user;

    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    int uid = s_v1_conn->send_uid++;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);

    if (transport == NULL) {
        return -1;
    }

    uint32_t version_bits = rolled_version ^ active_job->version;

    int ret = STRATUM_V1_submit_share(
        transport,
        uid,
        user,
        active_job->jobid,
        active_job->extranonce2,
        active_job->ntime,
        nonce,
        version_bits,
        sent_time_us);

    if (ret >= 0) {
        if (GLOBAL_STATE->SYSTEM_MODULE.shares_pending < UINT16_MAX) {
            GLOBAL_STATE->SYSTEM_MODULE.shares_pending++;
        }
    }
    return ret;
}

void stratum_v1_close_connection(GlobalState *GLOBAL_STATE)
{
    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    GLOBAL_STATE->transport = NULL;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);

    if (transport != NULL) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
    }
    if (s_v1_conn != NULL) {
        clear_active_job_ids(s_v1_conn->active_job_ids, &s_v1_conn->active_job_ids_count);
        free(s_v1_conn);
        s_v1_conn = NULL;
    }
    GLOBAL_STATE->SYSTEM_MODULE.shares_pending = 0;
    SYSTEM_clean_jobs_queue(GLOBAL_STATE);
    SYSTEM_reset_coinbase_ui_state(GLOBAL_STATE, "");
}

esp_err_t stratum_v1_run(GlobalState *GLOBAL_STATE, uint16_t pool_idx, volatile bool *should_reconnect)
{
    char *stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].url;
    uint16_t port = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].port;
    tls_mode tls = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].tls;
    char *cert = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].cert;
    char *username = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].user;
    char *password = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].pass;

    if (!stratum_url || stratum_url[0] == '\0' || port == 0) {
        ESP_LOGE(TAG, "Invalid pool configuration for pool %u", pool_idx);
        return ESP_ERR_INVALID_ARG;
    }

    GLOBAL_STATE->SYSTEM_MODULE.shares_pending = 0;
    STRATUM_V1_initialize_buffer();

    if (s_v1_conn != NULL) {
        clear_active_job_ids(s_v1_conn->active_job_ids, &s_v1_conn->active_job_ids_count);
        free(s_v1_conn);
    }
    s_v1_conn = calloc(1, sizeof(sv1_conn_t));
    if (!s_v1_conn) {
        ESP_LOGE(TAG, "Failed to allocate sv1_conn");
        return ESP_ERR_NO_MEM;
    }
    s_v1_conn->send_uid = 1;
    s_v1_conn->pool_difficulty = (double)GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty;
    s_v1_conn->version_mask = BIP320_VERSION_ROLLING_MASK;

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(stratum_url, port, &conn_info) != ESP_OK) {
        ESP_LOGE(TAG, "Address resolution failed for %s", stratum_url);
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV1: Pool unreachable");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, conn_info.host_ip);

    esp_transport_handle_t transport = STRATUM_V1_transport_init(tls, cert);
    if (!transport) {
        ESP_LOGE(TAG, "Transport initialization failed.");
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV1: Internal error");
        return ESP_FAIL;
    }

    if (tls != DISABLED) {
        esp_transport_ssl_set_common_name(transport, stratum_url);
    }

    esp_err_t ret = esp_transport_connect(transport, conn_info.host_ip, port, TRANSPORT_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Transport unable to connect to %s:%d (errno %d)", stratum_url, port, ret);
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV1: Connection failed");
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return ESP_FAIL;
    }

    stratum_socket_set_options(transport);

    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    GLOBAL_STATE->transport = transport;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);

    const char *protocol = (conn_info.addr_family == AF_INET6) ? "IPv6" : "IPv4";
    const char *tls_status = (tls == BUNDLED_CRT) ? " (TLS)" : (tls == CUSTOM_CRT) ? " (TLS Cert)" : "";
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info),
             "%s%s", protocol, tls_status);

    stratum_v1_reset_uid(GLOBAL_STATE);
    SYSTEM_clean_jobs_queue(GLOBAL_STATE);

    // mining.configure - ID: 1
    STRATUM_V1_configure_version_rolling(transport, stratum_get_next_uid(GLOBAL_STATE), &s_v1_conn->version_mask);

    // mining.subscribe - ID: 2
    STRATUM_V1_subscribe(transport, stratum_get_next_uid(GLOBAL_STATE), GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);

    int authorize_message_id = stratum_get_next_uid(GLOBAL_STATE);

    // mining.authorize - ID: 3
    STRATUM_V1_authorize(transport, authorize_message_id, username, password);

    esp_err_t run_result = ESP_OK;

    while (1) {
        if (should_reconnect && *should_reconnect) {
            ESP_LOGI(TAG, "Reconnect requested");
            run_result = ESP_OK;
            break;
        }

        if (!GLOBAL_STATE->ASIC_initalized) {
            ESP_LOGI(TAG, "Mining paused, disconnecting from pool");
            run_result = ESP_OK;
            break;
        }

        char *line = STRATUM_V1_receive_jsonrpc_line(transport);
        if (!line) {
            if (should_reconnect && *should_reconnect) {
                ESP_LOGI(TAG, "Reconnect requested during read");
                run_result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                run_result = ESP_FAIL;
            }
            break;
        }

        int64_t receive_time_us = esp_timer_get_time();
        bool reconnect_requested = false;

        if (!STRATUM_V1_parse(&stratum_api_v1_message, line)) {
            ESP_LOGE(TAG, "Failed to parse Stratum message, ignoring");
            STRATUM_V1_reset_message(&stratum_api_v1_message);
            free(line);
            continue;
        }
        free(line);

        switch (stratum_api_v1_message.method) {
            case METHOD_UNKNOWN:
                break;

            case MINING_NOTIFY: {
                miner_job_t *notify_job = &stratum_api_v1_message.mining_notification;
                bool is_duplicate = false;
                if (notify_job->job_id[0] != '\0') {
                    if (notify_job->clean_jobs) {
                        clear_active_job_ids(s_v1_conn->active_job_ids, &s_v1_conn->active_job_ids_count);
                    }
                    is_duplicate = !add_active_job_id(s_v1_conn->active_job_ids, &s_v1_conn->active_job_ids_count, notify_job->job_id);
                }

                if (is_duplicate) {
                    ESP_LOGW(TAG, "Ignoring duplicate notify for job %s", notify_job->job_id);
                } else {
                    GLOBAL_STATE->SYSTEM_MODULE.work_received++;
                    SYSTEM_notify_new_ntime(GLOBAL_STATE, notify_job->ntime);
                    if (notify_job->clean_jobs && (GLOBAL_STATE->stratum_queue.count > 0)) {
                        SYSTEM_clean_jobs_queue(GLOBAL_STATE);
                    }
                    if (GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
                        queue_dequeue(&GLOBAL_STATE->stratum_queue);
                    }
                    miner_job_t *job = miner_job_pool_next();
                    *job = *notify_job;
                    job->pool_id = (uint8_t)pool_idx;
                    job->pool_diff = s_v1_conn->pool_difficulty;
                    job->version_mask = s_v1_conn->version_mask;
                    job->extranonce1_len = s_v1_conn->extranonce1_len;
                    if (s_v1_conn->extranonce1_len > 0) {
                        memcpy(job->extranonce1, s_v1_conn->extranonce1, s_v1_conn->extranonce1_len);
                    }
                    job->extranonce2_len = s_v1_conn->extranonce2_len;

                    queue_enqueue(&GLOBAL_STATE->stratum_queue, job);
                }
                break;
            }

            case MINING_SET_DIFFICULTY:
                ESP_LOGI(TAG, "Set pool difficulty: %.2f", stratum_api_v1_message.new_difficulty);
                s_v1_conn->pool_difficulty = stratum_api_v1_message.new_difficulty;
                GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty = s_v1_conn->pool_difficulty;
                break;

            case MINING_SET_VERSION_MASK:
                ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
                s_v1_conn->version_mask = stratum_api_v1_message.version_mask;
                break;

            case STRATUM_RESULT_CONFIGURE:
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "Configure result accepted, version mask: %08lx", stratum_api_v1_message.version_mask);
                    s_v1_conn->version_mask = stratum_api_v1_message.version_mask;
                } else {
                    ESP_LOGW(TAG, "Configure result rejected: %s", stratum_api_v1_message.error_str);
                }
                break;

            case MINING_SET_EXTRANONCE:
            case STRATUM_RESULT_SUBSCRIBE:
                if (stratum_api_v1_message.extranonce_2_len < 0 || stratum_api_v1_message.extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                    ESP_LOGW(TAG, "Invalid extranonce_2_len %d, clamping to 0..%d",
                             stratum_api_v1_message.extranonce_2_len, MAX_EXTRANONCE_2_LEN);
                    stratum_api_v1_message.extranonce_2_len = (stratum_api_v1_message.extranonce_2_len < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
                }
                s_v1_conn->extranonce2_len = (uint8_t)stratum_api_v1_message.extranonce_2_len;
                if (stratum_api_v1_message.extranonce_str && stratum_api_v1_message.extranonce_str[0] != '\0') {
                    size_t slen = strlen(stratum_api_v1_message.extranonce_str) / 2;
                    if (slen > sizeof(s_v1_conn->extranonce1)) slen = sizeof(s_v1_conn->extranonce1);
                    hex2bin(stratum_api_v1_message.extranonce_str, s_v1_conn->extranonce1, slen);
                    s_v1_conn->extranonce1_len = (uint8_t)slen;
                } else {
                    s_v1_conn->extranonce1_len = 0;
                }
                ESP_LOGI(TAG, "Set extranonce: %s, extranonce_2_len: %d",
                         stratum_api_v1_message.extranonce_str ? stratum_api_v1_message.extranonce_str : "", s_v1_conn->extranonce2_len);
                break;

            case MINING_PING:
                STRATUM_V1_pong(transport, stratum_api_v1_message.message_id);
                break;

            case CLIENT_RECONNECT:
                ESP_LOGW(TAG, "Pool requested client reconnect, pausing 1s before reconnecting...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                reconnect_requested = true;
                break;

            case CLIENT_SHOW_MESSAGE:
                break;

            case CLIENT_GET_VERSION:
                STRATUM_V1_send_version(transport, stratum_api_v1_message.message_id);
                break;

            case STRATUM_RESULT: {
                float response_time_ms = STRATUM_V1_get_response_time_ms(stratum_api_v1_message.message_id, receive_time_us);
                if (response_time_ms >= 0) {
                    if (GLOBAL_STATE->SYSTEM_MODULE.shares_pending > 0) {
                        GLOBAL_STATE->SYSTEM_MODULE.shares_pending--;
                    }
                    if (stratum_api_v1_message.response_success) {
                        ESP_LOGI(TAG, "message result accepted");
                        ESP_LOGI(TAG, "Stratum response time: %.1f ms", response_time_ms);
                        GLOBAL_STATE->SYSTEM_MODULE.response_time = response_time_ms;
                        SYSTEM_notify_accepted_share(GLOBAL_STATE);
                    } else {
                        ESP_LOGW(TAG, "message result rejected: %s", stratum_api_v1_message.error_str);
                        SYSTEM_notify_rejected_share(GLOBAL_STATE, stratum_api_v1_message.error_str);
                    }
                } else {
                    if (stratum_api_v1_message.response_success) {
                        ESP_LOGI(TAG, "setup message accepted");
                        if (stratum_api_v1_message.message_id == authorize_message_id) {
                            uint16_t difficulty = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].difficulty;
                            if (difficulty > 0) {
                                STRATUM_V1_suggest_difficulty(transport, stratum_get_next_uid(GLOBAL_STATE), difficulty);
                            }
                            bool extranonce_subscribe = GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx].extranonce_subscribe;
                            if (extranonce_subscribe) {
                                STRATUM_V1_extranonce_subscribe(transport, stratum_get_next_uid(GLOBAL_STATE));
                            }
                        }
                    } else {
                        ESP_LOGE(TAG, "setup message rejected: %s", stratum_api_v1_message.error_str);
                        if (stratum_api_v1_message.message_id == authorize_message_id) {
                            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV1: Auth rejected");
                        }
                    }
                }
                break;
            }
        }

        STRATUM_V1_reset_message(&stratum_api_v1_message);
        if (reconnect_requested) {
            run_result = ESP_FAIL;
            break;
        }
    }

    stratum_v1_close_connection(GLOBAL_STATE);
    return run_result;
}
