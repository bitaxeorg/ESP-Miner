#include "esp_log.h"
#include "esp_system.h"
#include "system.h"
#include "global_state.h"
#include <lwip/tcpip.h>
#include "stratum_v1_task.h"
#include "stratum_socket.h"
#include "protocol_coordinator.h"
#include "connect.h"
#include "work_queue.h"
#include <esp_sntp.h>
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "coinbase_decoder.h"
#include <esp_heap_caps.h>
#include "esp_transport_ssl.h"
#include "freertos/task.h"

#define MAX_RETRY_ATTEMPTS 3
#define MAX_CRITICAL_RETRY_ATTEMPTS 5
#define MAX_EXTRANONCE_2_LEN 32

#define PORT CONFIG_STRATUM_PORT
#define STRATUM_URL CONFIG_STRATUM_URL
#define STRATUM_TLS CONFIG_STRATUM_TLS
#define STRATUM_CERT CONFIG_STRATUM_CERT

#define FALLBACK_PORT CONFIG_FALLBACK_STRATUM_PORT
#define FALLBACK_STRATUM_URL CONFIG_FALLBACK_STRATUM_URL
#define FALLBACK_STRATUM_TLS CONFIG_FALLBACK_STRATUM_TLS
#define FALLBACK_STRATUM_CERT CONFIG_FALLBACK_STRATUM_CERT

#define STRATUM_PW CONFIG_STRATUM_PW
#define FALLBACK_STRATUM_PW CONFIG_FALLBACK_STRATUM_PW
#define STRATUM_DIFFICULTY CONFIG_STRATUM_DIFFICULTY

#define TRANSPORT_TIMEOUT_MS 5000

#define BUFFER_SIZE 1024

static const char *TAG = "stratum_v1_task";

static StratumApiV1Message stratum_api_v1_message = {};

static int stratum_get_next_uid(GlobalState * GLOBAL_STATE)
{
    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    int uid = GLOBAL_STATE->send_uid++;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);
    return uid;
}


static void stratum_v1_reset_uid(GlobalState *GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Resetting stratum uid");
    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    GLOBAL_STATE->send_uid = 1;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);
}

void stratum_v1_close_connection(GlobalState *GLOBAL_STATE)
{
    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    GLOBAL_STATE->transport = NULL;
    taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);

    if (transport != NULL) {
        esp_transport_close(transport);
    }
    SYSTEM_clean_jobs_queue(GLOBAL_STATE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

static void decode_mining_notification(GlobalState * GLOBAL_STATE, const mining_notify *mining_notification)
{
    mining_notification_result_t *result = heap_caps_malloc(sizeof(mining_notification_result_t), MALLOC_CAP_SPIRAM);
    if (!result) {
        ESP_LOGE(TAG, "Failed to allocate result in PSRAM");
        return;
    }
    memset(result, 0, sizeof(mining_notification_result_t));

    const char *user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
    bool decode_coinbase_tx = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_decode_coinbase_tx : GLOBAL_STATE->SYSTEM_MODULE.pool_decode_coinbase_tx;

    if (coinbase_process_notification(mining_notification,
                                     GLOBAL_STATE->extranonce_str,
                                     GLOBAL_STATE->extranonce_2_len,
                                     user,
                                     decode_coinbase_tx,
                                     result) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to process mining notification");
        free(result);
        return;
    }

    // Update network difficulty
    GLOBAL_STATE->network_nonce_diff = (uint64_t) result->network_difficulty;
    suffixString(result->network_difficulty, GLOBAL_STATE->network_diff_string, DIFF_STRING_SIZE, 0);

    // Update block height
    if (result->block_height != GLOBAL_STATE->block_height) {
        ESP_LOGI(TAG, "Block height %d", result->block_height);
        GLOBAL_STATE->block_height = result->block_height;
    }

    // Update block signals (BIP-110, BIP-54, etc.)
    GLOBAL_STATE->block_signals_count = 0;
    if (result->bip54_signaling) {
        strncpy(GLOBAL_STATE->block_signals[GLOBAL_STATE->block_signals_count], "BIP-54", MAX_BLOCK_SIGNAL_LEN - 1);
        GLOBAL_STATE->block_signals[GLOBAL_STATE->block_signals_count][MAX_BLOCK_SIGNAL_LEN - 1] = '\0';
        GLOBAL_STATE->block_signals_count++;
        ESP_LOGI(TAG, "BIP-54 signaling detected");
    }
    if (result->bip110_signaling) {
        strncpy(GLOBAL_STATE->block_signals[GLOBAL_STATE->block_signals_count], "BIP-110", MAX_BLOCK_SIGNAL_LEN - 1);
        GLOBAL_STATE->block_signals[GLOBAL_STATE->block_signals_count][MAX_BLOCK_SIGNAL_LEN - 1] = '\0';
        GLOBAL_STATE->block_signals_count++;
        ESP_LOGI(TAG, "BIP-110 signaling detected");
    }

    // Update scriptsig
    if (result->scriptsig) {
        if (strcmp(result->scriptsig, GLOBAL_STATE->scriptsig) != 0) {
            ESP_LOGI(TAG, "Scriptsig: %s", result->scriptsig);
            strncpy(GLOBAL_STATE->scriptsig, result->scriptsig, sizeof(GLOBAL_STATE->scriptsig) - 1);
            GLOBAL_STATE->scriptsig[sizeof(GLOBAL_STATE->scriptsig) - 1] = '\0';
        }
        free(result->scriptsig);
    }

    // Update coinbase outputs
    // Safety guard: ensure output_count doesn't exceed array capacity
    if (result->output_count > MAX_COINBASE_TX_OUTPUTS) {
        result->output_count = MAX_COINBASE_TX_OUTPUTS;
    }

    GLOBAL_STATE->coinbase_value_total_satoshis = result->total_value_satoshis;
    ESP_LOGI(TAG, "Coinbase outputs: %d, total value: %llu%s", result->output_count, result->total_value_satoshis, result->decode_coinbase_tx ? " sats" : "");

    if (result->output_count != GLOBAL_STATE->coinbase_output_count ||
        memcmp(result->outputs, GLOBAL_STATE->coinbase_outputs, sizeof(coinbase_output_t) * result->output_count) != 0) {

        GLOBAL_STATE->coinbase_output_count = result->output_count;
        memcpy(GLOBAL_STATE->coinbase_outputs, result->outputs, sizeof(coinbase_output_t) * result->output_count);
        GLOBAL_STATE->coinbase_value_user_satoshis = result->user_value_satoshis;
        for (int i = 0; i < result->output_count; i++) {
            if (result->outputs[i].value_satoshis > 0) {
                if (result->outputs[i].is_user_output) {
                    ESP_LOGI(TAG, "  Output %d: %s (%llu sat) (Your payout address)", i, result->outputs[i].address, result->outputs[i].value_satoshis);
                } else {
                    ESP_LOGI(TAG, "  Output %d: %s (%llu sat)", i, result->outputs[i].address, result->outputs[i].value_satoshis);
                }
            } else {
                ESP_LOGI(TAG, "  Output %d: %s", i, result->outputs[i].address);
            }
        }
    }

    free(result);
}

void stratum_v1_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    bool use_fallback = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback;
    char *stratum_url = use_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url : GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    uint16_t port = use_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_port : GLOBAL_STATE->SYSTEM_MODULE.pool_port;

    // Set V1-specific free function for the work queue
    GLOBAL_STATE->stratum_queue.free_fn = (void (*)(void *))STRATUM_V1_free_mining_notify;

    STRATUM_V1_initialize_buffer();
    int retry_attempts = 0;
    int retry_critical_attempts = 0;

    ESP_LOGI(TAG, "Opening connection to pool: %s:%d", stratum_url, port);
    while (1) {
        // Check if coordinator wants us to shut down
        if (protocol_coordinator_v1_should_shutdown()) {
            ESP_LOGI(TAG, "Coordinator requested shutdown, exiting");
            protocol_coordinator_v1_exited();
            vTaskDelete(NULL);
        }

        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        if (retry_attempts >= MAX_RETRY_ATTEMPTS)
        {
            // Notify the coordinator and exit. The coordinator owns the
            // "all pools unreachable" decision, pool swapping, and power-pause
            // recovery — see protocol_coordinator.c.
            ESP_LOGW(TAG, "Max V1 retry attempts reached (%d), notifying coordinator", retry_attempts);
            stratum_v1_close_connection(GLOBAL_STATE);
            protocol_coordinator_notify_failure();
            vTaskDelete(NULL);
            return;
        }

        stratum_url = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url : GLOBAL_STATE->SYSTEM_MODULE.pool_url;
        port = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_port : GLOBAL_STATE->SYSTEM_MODULE.pool_port;

        stratum_connection_info_t conn_info;
        if (stratum_socket_resolve(stratum_url, port, &conn_info) != ESP_OK) {
            ESP_LOGE(TAG, "Address resolution failed for %s", stratum_url);
            retry_attempts++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, conn_info.host_ip);

        tls_mode tls = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_tls : GLOBAL_STATE->SYSTEM_MODULE.pool_tls;
        char * cert = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_cert : GLOBAL_STATE->SYSTEM_MODULE.pool_cert;
        retry_critical_attempts = 0;

        GLOBAL_STATE->transport = STRATUM_V1_transport_init(tls, cert);
        // Check if transport was initialized
        if (GLOBAL_STATE->transport == NULL) {
            ESP_LOGE(TAG, "Transport initialization failed.");
            if (++retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
                ESP_LOGE(TAG, "Max retry attempts reached, restarting...");
                esp_restart();
            }
            retry_attempts++;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        retry_critical_attempts = 0;

        // Use the already-resolved IP to avoid a second DNS lookup inside esp_transport_connect.
        // This prevents long DNS timeouts from blocking the lwIP stack and starving the HTTP server.
        if (tls != DISABLED) {
            esp_transport_ssl_set_common_name(GLOBAL_STATE->transport, stratum_url);
        }
        ESP_LOGI(TAG, "Transport initialized, connecting to %s:%d (%s)", stratum_url, port, conn_info.host_ip);
        esp_err_t ret = esp_transport_connect(GLOBAL_STATE->transport, conn_info.host_ip, port, TRANSPORT_TIMEOUT_MS);
        if (ret != ESP_OK) {
            retry_attempts++;
            ESP_LOGE(TAG, "Transport unable to connect to %s:%d (errno %d). Attempt: %d", stratum_url, port, ret, retry_attempts);
            // close the transport
            esp_transport_close(GLOBAL_STATE->transport);
            esp_transport_destroy(GLOBAL_STATE->transport);
            GLOBAL_STATE->transport = NULL;
            // instead of restarting, retry this every 5 seconds
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        stratum_socket_set_options(GLOBAL_STATE->transport);

        const char *protocol = (conn_info.addr_family == AF_INET6) ? "IPv6" : "IPv4";
        const char *tls_status;

        switch (tls) {
            case DISABLED:     tls_status = ""; break;
            case BUNDLED_CRT:  tls_status = " (TLS)"; break;
            case CUSTOM_CRT:   tls_status = " (TLS Cert)"; break;
            default:           tls_status = ""; break;
        }

        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info),
                 "%s%s", protocol, tls_status);

        stratum_v1_reset_uid(GLOBAL_STATE);
        SYSTEM_clean_jobs_queue(GLOBAL_STATE);

        ///// Start Stratum Action
        // mining.configure - ID: 1
        STRATUM_V1_configure_version_rolling(GLOBAL_STATE->transport, stratum_get_next_uid(GLOBAL_STATE), &GLOBAL_STATE->version_mask);

        // mining.subscribe - ID: 2
        STRATUM_V1_subscribe(GLOBAL_STATE->transport, stratum_get_next_uid(GLOBAL_STATE), GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);

        char *username = use_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
        char *password = use_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_pass : GLOBAL_STATE->SYSTEM_MODULE.pool_pass;

        int authorize_message_id = stratum_get_next_uid(GLOBAL_STATE);

        //mining.authorize - ID: 3
        STRATUM_V1_authorize(GLOBAL_STATE->transport, authorize_message_id, username, password);

        while (1) {
            // Check if coordinator wants us to shut down
            if (protocol_coordinator_v1_should_shutdown()) {
                ESP_LOGI(TAG, "Coordinator requested shutdown during recv loop, exiting");
                stratum_v1_close_connection(GLOBAL_STATE);
                protocol_coordinator_v1_exited();
                vTaskDelete(NULL);
            }

            char *line = STRATUM_V1_receive_jsonrpc_line(GLOBAL_STATE->transport);
            if (!line) {
                ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                retry_attempts++;
                stratum_v1_close_connection(GLOBAL_STATE);
                break;
            }

            if (!GLOBAL_STATE->ASIC_initalized) {
                free(line);
                ESP_LOGI(TAG, "Mining paused, disconnecting from pool");
                retry_attempts = 0;
                stratum_v1_close_connection(GLOBAL_STATE);
                break;
            }

            int64_t receive_time_us = esp_timer_get_time();

            STRATUM_V1_parse(&stratum_api_v1_message, line);
            free(line);

            if (stratum_api_v1_message.method == MINING_NOTIFY) {
                GLOBAL_STATE->SYSTEM_MODULE.work_received++;
                SYSTEM_notify_new_ntime(GLOBAL_STATE, stratum_api_v1_message.mining_notification->ntime);
                if (stratum_api_v1_message.mining_notification->clean_jobs &&
                    (GLOBAL_STATE->stratum_queue.count > 0)) {
                    SYSTEM_clean_jobs_queue(GLOBAL_STATE);
                }
                if (GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
                    mining_notify *next_notify_json_str = (mining_notify *) queue_dequeue(&GLOBAL_STATE->stratum_queue);
                    STRATUM_V1_free_mining_notify(next_notify_json_str);
                }
                queue_enqueue(&GLOBAL_STATE->stratum_queue, stratum_api_v1_message.mining_notification);
                decode_mining_notification(GLOBAL_STATE, stratum_api_v1_message.mining_notification);
                stratum_api_v1_message.mining_notification = NULL;
            } else if (stratum_api_v1_message.method == MINING_SET_DIFFICULTY) {
                ESP_LOGI(TAG, "Set pool difficulty: %.2f", stratum_api_v1_message.new_difficulty);
                GLOBAL_STATE->pool_difficulty = stratum_api_v1_message.new_difficulty;
                GLOBAL_STATE->new_set_mining_difficulty_msg = true;
            } else if (stratum_api_v1_message.method == MINING_SET_VERSION_MASK ||
                    stratum_api_v1_message.method == STRATUM_RESULT_VERSION_MASK) {
                ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
                GLOBAL_STATE->version_mask = stratum_api_v1_message.version_mask;
                GLOBAL_STATE->new_stratum_version_rolling_msg = true;
            } else if (stratum_api_v1_message.method == MINING_SET_EXTRANONCE ||
                    stratum_api_v1_message.method == STRATUM_RESULT_SUBSCRIBE) {
                // Validate extranonce_2_len to prevent buffer overflow
                if (stratum_api_v1_message.extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                    ESP_LOGW(TAG, "Extranonce_2_len %d exceeds maximum %d, clamping to maximum",
                             stratum_api_v1_message.extranonce_2_len, MAX_EXTRANONCE_2_LEN);
                    stratum_api_v1_message.extranonce_2_len = MAX_EXTRANONCE_2_LEN;
                }
                ESP_LOGI(TAG, "Set extranonce: %s, extranonce_2_len: %d", stratum_api_v1_message.extranonce_str, stratum_api_v1_message.extranonce_2_len);
                char *old_extranonce_str = GLOBAL_STATE->extranonce_str;
                GLOBAL_STATE->extranonce_str = stratum_api_v1_message.extranonce_str;
                stratum_api_v1_message.extranonce_str = NULL;
                GLOBAL_STATE->extranonce_2_len = stratum_api_v1_message.extranonce_2_len;
                free(old_extranonce_str);
            } else if (stratum_api_v1_message.method == MINING_PING) {
                STRATUM_V1_pong(GLOBAL_STATE->transport, stratum_api_v1_message.message_id);
            } else if (stratum_api_v1_message.method == CLIENT_RECONNECT) {
                ESP_LOGE(TAG, "Pool requested client reconnect...");
                stratum_v1_close_connection(GLOBAL_STATE);
                break;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT) {
                float response_time_ms = STRATUM_V1_get_response_time_ms(stratum_api_v1_message.message_id, receive_time_us);
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "message result accepted");
                    if (response_time_ms >= 0) {
                        ESP_LOGI(TAG, "Stratum response time: %.1f ms", response_time_ms);
                        GLOBAL_STATE->SYSTEM_MODULE.response_time = response_time_ms;
                        SYSTEM_notify_accepted_share(GLOBAL_STATE);
                    }
                } else {
                    ESP_LOGW(TAG, "message result rejected: %s", stratum_api_v1_message.error_str);
                    if (response_time_ms >= 0) {
                        SYSTEM_notify_rejected_share(GLOBAL_STATE, stratum_api_v1_message.error_str);
                    }
                }
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SETUP) {
                // Reset retry attempts after successfully receiving data.
                retry_attempts = 0;
                // Tell the coordinator setup succeeded so it clears its
                // failure counter and pools_unavailable.
                protocol_coordinator_notify_success();
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "setup message accepted");
                    uint16_t difficulty = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_difficulty : GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty;
                    if (stratum_api_v1_message.message_id == authorize_message_id && difficulty > 0) {
                        STRATUM_V1_suggest_difficulty(GLOBAL_STATE->transport, stratum_get_next_uid(GLOBAL_STATE), difficulty);
                    }
                    bool extranonce_subscribe = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_extranonce_subscribe : GLOBAL_STATE->SYSTEM_MODULE.pool_extranonce_subscribe;
                    if (extranonce_subscribe) {
                        STRATUM_V1_extranonce_subscribe(GLOBAL_STATE->transport, stratum_get_next_uid(GLOBAL_STATE));
                    }
                } else {
                    ESP_LOGE(TAG, "setup message rejected: %s", stratum_api_v1_message.error_str);
                }
            }
            STRATUM_V1_reset_message(&stratum_api_v1_message);
        }
    }
    vTaskDelete(NULL);
}
