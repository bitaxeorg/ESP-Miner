#include "esp_log.h"
#include "system.h"
#include "global_state.h"
#include <lwip/tcpip.h>
#include "stratum_v1_client.h"
#include "stratum_task.h"
#include "stratum_api.h"
#include "stratum_socket.h"
#include "connect.h"
#include <esp_sntp.h>
#include "esp_timer.h"
#include "esp_transport.h"
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "miner_job.h"
#include <esp_heap_caps.h>
#include "esp_transport_ssl.h"
#include "freertos/task.h"

#define MAX_EXTRANONCE_2_LEN 32
#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define PROBE_RECV_BUFFER_SIZE 2048

static const char *TAG = "stratum_v1";

static StratumApiV1Message *s_v1_msg = NULL;
static sv1_conn_t *s_v1_conn = NULL;

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

static int stratum_get_next_uid(GlobalState * GLOBAL_STATE)
{
    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    int uid = s_v1_conn ? s_v1_conn->send_uid++ : 1;
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
    return uid;
}

static void stratum_v1_reset_uid(GlobalState *GLOBAL_STATE)
{
    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    if (s_v1_conn) s_v1_conn->send_uid = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
}

int stratum_v1_submit_share(GlobalState *GLOBAL_STATE, const bm_job *active_job,
                            uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us)
{
    if (!GLOBAL_STATE || !active_job) return -1;
    uint32_t version_bits = rolled_version ^ active_job->version;

    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    if (transport == NULL || s_v1_conn == NULL) {
        pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
        return -1;
    }

    int uid = s_v1_conn->send_uid++;
    int ret = STRATUM_V1_submit_share(
        transport,
        uid,
        s_v1_conn->user,
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
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
    return ret;
}

void stratum_v1_close_connection(GlobalState *GLOBAL_STATE)
{
    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    GLOBAL_STATE->transport = NULL;
    sv1_conn_t *conn = s_v1_conn;
    s_v1_conn = NULL;

    if (transport != NULL) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
    }
    if (conn != NULL) {
        clear_active_job_ids(conn->active_job_ids, &conn->active_job_ids_count);
        free(conn);
    }
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);

    GLOBAL_STATE->SYSTEM_MODULE.shares_pending = 0;
    SYSTEM_clean_jobs_queue(GLOBAL_STATE);
    SYSTEM_reset_coinbase_ui_state(GLOBAL_STATE, "");
}

esp_err_t stratum_v1_run(GlobalState *GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE || pool_idx >= MAX_POOLS) {
        return ESP_ERR_INVALID_ARG;
    }

    PoolConfig *pool = &GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx];
    char stratum_url[256] = {0};
    char username[256] = {0};
    char password[256] = {0};
    char cert_buf[512] = {0};

    if (pool->url) strlcpy(stratum_url, pool->url, sizeof(stratum_url));
    if (pool->user) strlcpy(username, pool->user, sizeof(username));
    if (pool->pass) strlcpy(password, pool->pass, sizeof(password));
    if (pool->cert) strlcpy(cert_buf, pool->cert, sizeof(cert_buf));

    uint16_t port = pool->port;
    tls_mode tls = (tls_mode)pool->tls;
    uint16_t difficulty = pool->difficulty;
    bool extranonce_subscribe = pool->extranonce_subscribe;

    if (stratum_url[0] == '\0' || port == 0) {
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
    strlcpy(s_v1_conn->user, username, sizeof(s_v1_conn->user));
    s_v1_conn->pool_difficulty = (double)GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty;
    s_v1_conn->version_mask = 0;

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(stratum_url, port, &conn_info) != ESP_OK) {
        ESP_LOGE(TAG, "Address resolution failed for %s", stratum_url);
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV1: Pool unreachable");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, conn_info.host_ip);

    esp_transport_handle_t transport = STRATUM_V1_transport_init(tls, cert_buf[0] ? cert_buf : NULL);
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

    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    GLOBAL_STATE->transport = transport;
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);

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

    if (!s_v1_msg) {
        s_v1_msg = heap_caps_calloc(1, sizeof(StratumApiV1Message), MALLOC_CAP_SPIRAM);
        if (!s_v1_msg) {
            s_v1_msg = calloc(1, sizeof(StratumApiV1Message));
        }
        if (!s_v1_msg) {
            ESP_LOGE(TAG, "Failed to allocate StratumApiV1Message");
            esp_transport_close(transport);
            esp_transport_destroy(transport);
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t run_result = ESP_OK;

    while (1) {
        if (stratum_reconnect_requested()) {
            ESP_LOGI(TAG, "Reconnect requested");
            run_result = ESP_OK;
            break;
        }

        if (GLOBAL_STATE->SYSTEM_MODULE.mining_paused || GLOBAL_STATE->SYSTEM_MODULE.hardware_fault) {
            ESP_LOGI(TAG, "Mining paused, disconnecting from pool");
            run_result = ESP_OK;
            break;
        }

        char *line = STRATUM_V1_receive_jsonrpc_line(transport);
        if (!line) {
            if (stratum_reconnect_requested()) {
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

        uint8_t target_slot = (GLOBAL_STATE->active_job_slot_idx + 1) % 2;
        miner_job_t *target_job = miner_job_get_slot(target_slot);

        if (!STRATUM_V1_parse(s_v1_msg, line, target_job)) {
            ESP_LOGE(TAG, "Failed to parse Stratum message, ignoring");
            STRATUM_V1_reset_message(s_v1_msg);
            free(line);
            continue;
        }
        free(line);

        switch (s_v1_msg->method) {
            case METHOD_UNKNOWN:
                break;

            case MINING_NOTIFY: {
                bool is_duplicate = false;
                if (target_job->job_id[0] != '\0') {
                    if (target_job->clean_jobs) {
                        clear_active_job_ids(s_v1_conn->active_job_ids, &s_v1_conn->active_job_ids_count);
                    }
                    is_duplicate = !add_active_job_id(s_v1_conn->active_job_ids, &s_v1_conn->active_job_ids_count, target_job->job_id);
                }

                if (is_duplicate) {
                    ESP_LOGW(TAG, "Ignoring duplicate notify for job %s", target_job->job_id);
                } else {
                    GLOBAL_STATE->SYSTEM_MODULE.work_received++;
                    SYSTEM_notify_new_ntime(GLOBAL_STATE, target_job->ntime);
                    
                    target_job->pool_id = (uint8_t)pool_idx;
                    target_job->pool_diff = s_v1_conn->pool_difficulty;
                    target_job->version_mask = s_v1_conn->version_mask;
                    target_job->extranonce1_len = s_v1_conn->extranonce1_len;
                    if (s_v1_conn->extranonce1_len > 0) {
                        memcpy(target_job->extranonce1, s_v1_conn->extranonce1, s_v1_conn->extranonce1_len);
                    }
                    target_job->extranonce2_len = s_v1_conn->extranonce2_len;

                    if (GLOBAL_STATE->create_jobs_task_handle) {
                        xTaskNotify(GLOBAL_STATE->create_jobs_task_handle, target_slot, eSetValueWithOverwrite);
                    }
                }
                break;
            }

            case MINING_SET_DIFFICULTY:
                ESP_LOGI(TAG, "Set pool difficulty: %.2f", s_v1_msg->new_difficulty);
                s_v1_conn->pool_difficulty = s_v1_msg->new_difficulty;
                GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty = s_v1_conn->pool_difficulty;
                break;

            case MINING_SET_VERSION_MASK:
                ESP_LOGI(TAG, "Set version mask: %08lx", s_v1_msg->version_mask);
                s_v1_conn->version_mask = s_v1_msg->version_mask;
                break;

            case STRATUM_RESULT_CONFIGURE:
                if (s_v1_msg->response_success) {
                    ESP_LOGI(TAG, "Configure result accepted, version mask: %08lx", s_v1_msg->version_mask);
                    s_v1_conn->version_mask = s_v1_msg->version_mask;
                } else {
                    ESP_LOGW(TAG, "Configure result rejected: %s", s_v1_msg->error_str);
                }
                break;

            case MINING_SET_EXTRANONCE:
            case STRATUM_RESULT_SUBSCRIBE:
                if (s_v1_msg->extranonce_2_len < 0 || s_v1_msg->extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                    ESP_LOGW(TAG, "Invalid extranonce_2_len %d, clamping to 0..%d",
                             s_v1_msg->extranonce_2_len, MAX_EXTRANONCE_2_LEN);
                    s_v1_msg->extranonce_2_len = (s_v1_msg->extranonce_2_len < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
                }
                s_v1_conn->extranonce2_len = (uint8_t)s_v1_msg->extranonce_2_len;
                if (s_v1_msg->extranonce_str && s_v1_msg->extranonce_str[0] != '\0') {
                    size_t slen = strlen(s_v1_msg->extranonce_str) / 2;
                    if (slen > sizeof(s_v1_conn->extranonce1)) slen = sizeof(s_v1_conn->extranonce1);
                    hex2bin(s_v1_msg->extranonce_str, s_v1_conn->extranonce1, slen);
                    s_v1_conn->extranonce1_len = (uint8_t)slen;
                } else {
                    s_v1_conn->extranonce1_len = 0;
                }
                ESP_LOGI(TAG, "Set extranonce: %s, extranonce_2_len: %d",
                         s_v1_msg->extranonce_str ? s_v1_msg->extranonce_str : "", s_v1_conn->extranonce2_len);
                break;

            case MINING_PING:
                STRATUM_V1_pong(transport, s_v1_msg->message_id);
                break;

            case CLIENT_RECONNECT:
                ESP_LOGW(TAG, "Pool requested client reconnect, pausing 1s before reconnecting...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                reconnect_requested = true;
                break;

            case CLIENT_SHOW_MESSAGE:
                break;

            case CLIENT_GET_VERSION:
                STRATUM_V1_send_version(transport, s_v1_msg->message_id);
                break;

            case STRATUM_RESULT: {
                float response_time_ms = STRATUM_V1_get_response_time_ms(s_v1_msg->message_id, receive_time_us);
                if (response_time_ms >= 0) {
                    if (GLOBAL_STATE->SYSTEM_MODULE.shares_pending > 0) {
                        GLOBAL_STATE->SYSTEM_MODULE.shares_pending--;
                    }
                    if (s_v1_msg->response_success) {
                        ESP_LOGI(TAG, "message result accepted");
                        ESP_LOGI(TAG, "Stratum response time: %.1f ms", response_time_ms);
                        GLOBAL_STATE->SYSTEM_MODULE.response_time = response_time_ms;
                        SYSTEM_notify_accepted_share(GLOBAL_STATE);
                    } else {
                        ESP_LOGW(TAG, "message result rejected: %s", s_v1_msg->error_str);
                        SYSTEM_notify_rejected_share(GLOBAL_STATE, s_v1_msg->error_str);
                    }
                } else {
                    if (s_v1_msg->response_success) {
                        ESP_LOGI(TAG, "setup message accepted");
                        if (s_v1_msg->message_id == authorize_message_id) {
                            if (difficulty > 0) {
                                STRATUM_V1_suggest_difficulty(transport, stratum_get_next_uid(GLOBAL_STATE), difficulty);
                            }
                            if (extranonce_subscribe) {
                                STRATUM_V1_extranonce_subscribe(transport, stratum_get_next_uid(GLOBAL_STATE));
                            }
                        }
                    } else {
                        ESP_LOGE(TAG, "setup message rejected: %s", s_v1_msg->error_str);
                        if (s_v1_msg->message_id == authorize_message_id) {
                            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV1: Auth rejected");
                        }
                    }
                }
                break;
            }
        }

        STRATUM_V1_reset_message(s_v1_msg);
        if (reconnect_requested) {
            run_result = ESP_FAIL;
            break;
        }
    }

    stratum_v1_close_connection(GLOBAL_STATE);
    return run_result;
}

bool stratum_v1_probe_pool(GlobalState *GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE || pool_idx >= MAX_POOLS) return false;

    PoolConfig *pool = &GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx];
    char url[256] = {0};
    char user[256] = {0};
    char pass[256] = {0};
    char cert_buf[512] = {0};

    if (pool->url) strlcpy(url, pool->url, sizeof(url));
    if (pool->user) strlcpy(user, pool->user, sizeof(user));
    if (pool->pass) strlcpy(pass, pool->pass, sizeof(pass));
    if (pool->cert) strlcpy(cert_buf, pool->cert, sizeof(cert_buf));

    uint16_t port = pool->port;
    tls_mode tls = (tls_mode)pool->tls;

    if (url[0] == '\0' || port == 0) return false;

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(url, port, &conn_info) != ESP_OK) {
        return false;
    }

    esp_transport_handle_t transport = STRATUM_V1_transport_init(tls, cert_buf[0] ? cert_buf : NULL);
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

    STRATUM_V1_subscribe(transport, 1, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    STRATUM_V1_authorize(transport, 2, user, pass);

    char recv_buf[PROBE_RECV_BUFFER_SIZE];
    int total_bytes = 0;
    bool success = false;
    int64_t start_time = esp_timer_get_time();

    while (total_bytes < (int)sizeof(recv_buf) - 1) {
        int64_t elapsed_ms = (esp_timer_get_time() - start_time) / 1000;
        int remaining_ms = TRANSPORT_TIMEOUT_MS - (int)elapsed_ms;
        if (remaining_ms <= 0) break;

        int n = esp_transport_read(transport, recv_buf + total_bytes, sizeof(recv_buf) - 1 - total_bytes, remaining_ms);
        if (n <= 0) break;
        total_bytes += n;
        recv_buf[total_bytes] = '\0';

        // Reject immediately if an error payload is returned
        if (strstr(recv_buf, "\"error\":[") != NULL ||
            strstr(recv_buf, "\"error\": {") != NULL ||
            strstr(recv_buf, "\"error\":{") != NULL) {
            success = false;
            break;
        }

        // Accept if a result is returned for subscribe (id: 1) or authorize (id: 2)
        if (strstr(recv_buf, "\"result\"") != NULL &&
            (strstr(recv_buf, "\"id\":1") != NULL || strstr(recv_buf, "\"id\": 1") != NULL ||
             strstr(recv_buf, "\"id\":2") != NULL || strstr(recv_buf, "\"id\": 2") != NULL)) {
            success = true;
            break;
        }
    }

    esp_transport_close(transport);
    esp_transport_destroy(transport);
    return success;
}
