#include "esp_log.h"
#include "connect.h"
#include "system.h"
#include "global_state.h"
#include "lwip/dns.h"
#include <lwip/tcpip.h>
#include "nvs_config.h"
#include "stratum_task.h"
#include "work_queue.h"
#include "esp_wifi.h"
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>
#include "esp_timer.h"
#include <stdbool.h>

#define MAX_RETRY_ATTEMPTS 3
#define MAX_CRITICAL_RETRY_ATTEMPTS 5
#define MAX_EXTRANONCE_2_LEN 32

#define BUFFER_SIZE 1024

static const char * TAG = "stratum_task";

static StratumApiV1Message stratum_api_v1_message = {};

static const char * primary_stratum_url;
static uint16_t primary_stratum_port;

struct timeval tcp_snd_timeout = {
    .tv_sec = 5,
    .tv_usec = 0
};

struct timeval tcp_rcv_timeout = {
    .tv_sec = 60 * 10,
    .tv_usec = 0
};

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return true;
    } else {
        return false;
    }
}

void cleanQueue(GlobalState * GLOBAL_STATE) {
    ESP_LOGI(TAG, "Clean Jobs: clearing queue");
    GLOBAL_STATE->abandon_work = 1;
    queue_clear(&GLOBAL_STATE->stratum_queue);

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    ASIC_jobs_queue_clear(&GLOBAL_STATE->ASIC_jobs_queue);
    for (int i = 0; i < 128; i = i + 4) {
        GLOBAL_STATE->valid_jobs[i] = 0;
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
}

void stratum_reset_uid(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Resetting stratum uid");
    GLOBAL_STATE->send_uid = 1;
}


void stratum_close_connection(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->sock < 0) {
        ESP_LOGE(TAG, "Socket already shutdown, not shutting down again..");
        return;
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(GLOBAL_STATE->sock, SHUT_RDWR);
    close(GLOBAL_STATE->sock);
    cleanQueue(GLOBAL_STATE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void stratum_primary_heartbeat(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_LOGI(TAG, "Starting heartbeat thread for primary pool: %s:%d", primary_stratum_url, primary_stratum_port);
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;

    struct timeval tcp_timeout = {
        .tv_sec = 5,
        .tv_usec = 0
    };

    while (1)
    {
        if (GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback == false) {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        char host_ip[INET_ADDRSTRLEN];
        ESP_LOGD(TAG, "Running Heartbeat on: %s!", primary_stratum_url);

        if (!is_wifi_connected()) {
            ESP_LOGD(TAG, "Heartbeat. Failed WiFi check!");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        struct hostent *primary_dns_addr = gethostbyname(primary_stratum_url);
        if (primary_dns_addr == NULL) {
            ESP_LOGD(TAG, "Heartbeat. Failed DNS check for: %s!", primary_stratum_url);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }
        inet_ntop(AF_INET, (void *)primary_dns_addr->h_addr_list[0], host_ip, sizeof(host_ip));

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(primary_stratum_port);

        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGD(TAG, "Heartbeat. Failed socket create check!");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            ESP_LOGD(TAG, "Heartbeat. Failed connect check: %s:%d (errno %d: %s)", host_ip, primary_stratum_port, errno, strerror(errno));
            close(sock);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO , &tcp_timeout, sizeof(tcp_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
        }

        int send_uid = 1;
        STRATUM_V1_subscribe(sock, send_uid++, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_authorize(sock, send_uid++, GLOBAL_STATE->SYSTEM_MODULE.pool_user, GLOBAL_STATE->SYSTEM_MODULE.pool_pass);

        char recv_buffer[BUFFER_SIZE];
        memset(recv_buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, recv_buffer, BUFFER_SIZE - 1, 0);

        shutdown(sock, SHUT_RDWR);
        close(sock);

        if (bytes_received == -1)  {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        if (strstr(recv_buffer, "mining.notify") != NULL) {
            ESP_LOGI(TAG, "Heartbeat successful and in fallback mode. Switching back to primary.");
            GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = false;
            stratum_close_connection(GLOBAL_STATE);
            continue;
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

void stratum_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    primary_stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    primary_stratum_port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;
    char * stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    uint16_t port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;
    bool extranonce_subscribe = GLOBAL_STATE->SYSTEM_MODULE.pool_extranonce_subscribe;
    uint16_t difficulty = GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty;

    STRATUM_V1_initialize_buffer();
    char host_ip[20];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    int retry_attempts = 0;
    int retry_critical_attempts = 0;


    xTaskCreate(stratum_primary_heartbeat, "stratum primary heartbeat", 8192, pvParameters, 1, NULL);

    ESP_LOGI(TAG, "Opening connection to pool: %s:%d", stratum_url, port);
    while (1) {
        if (!is_wifi_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        if (retry_attempts >= MAX_RETRY_ATTEMPTS)
        {
            if (GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url == NULL || GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url[0] == '\0') {
                ESP_LOGI(TAG, "Unable to switch to fallback. No url configured. (retries: %d)...", retry_attempts);
                GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = false;
                retry_attempts = 0;
                continue;
            }

            GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = !GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback;
            
            // Reset share stats at failover
            for (int i = 0; i < GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats_count; i++) {
                GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats[i].count = 0;
                GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats[i].message[0] = '\0';
            }
            GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats_count = 0;
            GLOBAL_STATE->SYSTEM_MODULE.shares_accepted = 0;
            GLOBAL_STATE->SYSTEM_MODULE.shares_rejected = 0;

            ESP_LOGI(TAG, "Switching target due to too many failures (retries: %d)...", retry_attempts);
            retry_attempts = 0;
        }

        stratum_url = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url : GLOBAL_STATE->SYSTEM_MODULE.pool_url;
        port = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_port : GLOBAL_STATE->SYSTEM_MODULE.pool_port;
        extranonce_subscribe = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_extranonce_subscribe : GLOBAL_STATE->SYSTEM_MODULE.pool_extranonce_subscribe;
        difficulty = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_difficulty : GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty;

        struct hostent *dns_addr = gethostbyname(stratum_url);
        if (dns_addr == NULL) {
            retry_attempts++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        inet_ntop(AF_INET, (void *)dns_addr->h_addr_list[0], host_ip, sizeof(host_ip));

        ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, host_ip);

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);

        GLOBAL_STATE->sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        if (GLOBAL_STATE->sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            if (++retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
                ESP_LOGE(TAG, "Max retry attempts reached, restarting...");
                esp_restart();
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        retry_critical_attempts = 0;

        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, port);
        int err = connect(GLOBAL_STATE->sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            retry_attempts++;
            ESP_LOGE(TAG, "Socket unable to connect to %s:%d (errno %d: %s)", stratum_url, port, errno, strerror(errno));
            // close the socket
            shutdown(GLOBAL_STATE->sock, SHUT_RDWR);
            close(GLOBAL_STATE->sock);
            // instead of restarting, retry this every 5 seconds
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (setsockopt(GLOBAL_STATE->sock, SOL_SOCKET, SO_SNDTIMEO, &tcp_snd_timeout, sizeof(tcp_snd_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_SNDTIMEO");
        }

        if (setsockopt(GLOBAL_STATE->sock, SOL_SOCKET, SO_RCVTIMEO , &tcp_rcv_timeout, sizeof(tcp_rcv_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
        }

        stratum_reset_uid(GLOBAL_STATE);
        cleanQueue(GLOBAL_STATE);

        ///// Start Stratum Action
        // mining.configure - ID: 1
        STRATUM_V1_configure_version_rolling(GLOBAL_STATE->sock, GLOBAL_STATE->send_uid++, &GLOBAL_STATE->version_mask);

        // mining.subscribe - ID: 2
        STRATUM_V1_subscribe(GLOBAL_STATE->sock, GLOBAL_STATE->send_uid++, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);

        char * username = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
        char * password = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_pass : GLOBAL_STATE->SYSTEM_MODULE.pool_pass;

        int authorize_message_id = GLOBAL_STATE->send_uid++;
        //mining.authorize - ID: 3
        STRATUM_V1_authorize(GLOBAL_STATE->sock, authorize_message_id, username, password);
        STRATUM_V1_stamp_tx(authorize_message_id);

        // Everything is set up, lets make sure we don't abandon work unnecessarily.
        GLOBAL_STATE->abandon_work = 0;

        while (1) {
            char * line = STRATUM_V1_receive_jsonrpc_line(GLOBAL_STATE->sock);
            if (!line) {
                ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                retry_attempts++;
                stratum_close_connection(GLOBAL_STATE);
                break;
            }

            double response_time_ms = STRATUM_V1_get_response_time_ms(stratum_api_v1_message.message_id);
            if (response_time_ms >= 0) {
                ESP_LOGI(TAG, "Stratum response time: %.2f ms", response_time_ms);
                GLOBAL_STATE->SYSTEM_MODULE.response_time = response_time_ms;
            }

            STRATUM_V1_parse(&stratum_api_v1_message, line);
            free(line);

            if (stratum_api_v1_message.method == MINING_NOTIFY) {
                SYSTEM_notify_new_ntime(GLOBAL_STATE, stratum_api_v1_message.mining_notification->ntime);
                if (stratum_api_v1_message.should_abandon_work &&
                    (GLOBAL_STATE->stratum_queue.count > 0 || GLOBAL_STATE->ASIC_jobs_queue.count > 0)) {
                    cleanQueue(GLOBAL_STATE);
                }
                if (GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
                    mining_notify * next_notify_json_str = (mining_notify *) queue_dequeue(&GLOBAL_STATE->stratum_queue);
                    STRATUM_V1_free_mining_notify(next_notify_json_str);
                }
                queue_enqueue(&GLOBAL_STATE->stratum_queue, stratum_api_v1_message.mining_notification);
            } else if (stratum_api_v1_message.method == MINING_SET_DIFFICULTY) {
                ESP_LOGI(TAG, "Set pool difficulty: %ld", stratum_api_v1_message.new_difficulty);
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
                char * old_extranonce_str = GLOBAL_STATE->extranonce_str;
                GLOBAL_STATE->extranonce_str = stratum_api_v1_message.extranonce_str;
                GLOBAL_STATE->extranonce_2_len = stratum_api_v1_message.extranonce_2_len;
                free(old_extranonce_str);
            } else if (stratum_api_v1_message.method == CLIENT_RECONNECT) {
                ESP_LOGE(TAG, "Pool requested client reconnect...");
                stratum_close_connection(GLOBAL_STATE);
                break;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT) {
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "message result accepted");
                    SYSTEM_notify_accepted_share(GLOBAL_STATE);
                } else {
                    ESP_LOGW(TAG, "message result rejected: %s", stratum_api_v1_message.error_str);
                    SYSTEM_notify_rejected_share(GLOBAL_STATE, stratum_api_v1_message.error_str);
                }
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SETUP) {
                // Reset retry attempts after successfully receiving data.
                retry_attempts = 0;
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "setup message accepted");
                    if (stratum_api_v1_message.message_id == authorize_message_id) {
                        STRATUM_V1_suggest_difficulty(GLOBAL_STATE->sock, GLOBAL_STATE->send_uid++, difficulty);
                    }
                    if (extranonce_subscribe) {
                        STRATUM_V1_extranonce_subscribe(GLOBAL_STATE->sock, GLOBAL_STATE->send_uid++);
                    }
                } else {
                    ESP_LOGE(TAG, "setup message rejected: %s", stratum_api_v1_message.error_str);
                }
            }
        }
    }
    vTaskDelete(NULL);
}
