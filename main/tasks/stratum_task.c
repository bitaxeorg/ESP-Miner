#include "esp_log.h"
#include "connect.h"
#include "system.h"

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
#include "utils.h"
#include "asic_task_module.h"
#include "system_module.h"
#include "mining_module.h"
#include "device_config.h"
#include "pool_module.h"

#define MAX_RETRY_ATTEMPTS 3
#define MAX_CRITICAL_RETRY_ATTEMPTS 5
#define MAX_EXTRANONCE_2_LEN 32

#define BUFFER_SIZE 1024

static const char * TAG = "stratum_task";

static StratumApiV1Message stratum_api_v1_message = {};

static const char * primary_stratum_url;
static uint16_t primary_stratum_port;

 //maybe need a stratum module
int sock;

// A message ID that must be unique per request that expects a response.
// For requests not expecting a response (called notifications), this is null.
int send_uid;

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

void cleanQueue() {
    ESP_LOGI(TAG, "Clean Jobs: clearing queue");
    MINING_MODULE.abandon_work = 1;
    queue_clear(&MINING_MODULE.stratum_queue);

    pthread_mutex_lock(&ASIC_TASK_MODULE.valid_jobs_lock);
    ASIC_jobs_queue_clear(&MINING_MODULE.ASIC_jobs_queue);
    for (int i = 0; i < 128; i = i + 4) {
        ASIC_TASK_MODULE.valid_jobs[i] = 0;
    }
    pthread_mutex_unlock(&ASIC_TASK_MODULE.valid_jobs_lock);
}

void stratum_reset_uid()
{
    ESP_LOGI(TAG, "Resetting stratum uid");
    send_uid = 1;
}


void stratum_close_connection()
{
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket already shutdown, not shutting down again..");
        return;
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    cleanQueue();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

int stratum_submit_share(char * jobid, char * extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version)
{
    char * user = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].user;
    int ret = STRATUM_V1_submit_share(
        sock,
        send_uid++,
        user,
        jobid,
        extranonce2,
        ntime,
        nonce,
        version);

    if (ret < 0) {
        ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
        stratum_close_connection();
    }
    return ret;
}

void stratum_primary_heartbeat()
{

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
        if (POOL_MODULE.active_pool_idx == POOL_MODULE.default_pool_idx) {
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
        STRATUM_V1_subscribe(sock, send_uid++, DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_authorize(sock, send_uid++, POOL_MODULE.pools[POOL_MODULE.active_pool_idx].user, POOL_MODULE.pools[POOL_MODULE.active_pool_idx].pass);

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
            stratum_close_connection();
            continue;
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

void decode_mining_notification(const mining_notify *mining_notification)
{
    double network_difficulty = networkDifficulty(mining_notification->target);
    suffixString(network_difficulty, SYSTEM_MODULE.network_diff_string, DIFF_STRING_SIZE, 0);    

    int coinbase_1_len = strlen(mining_notification->coinbase_1) / 2;
    int coinbase_2_len = strlen(mining_notification->coinbase_2) / 2;
    
    int coinbase_1_offset = 41; // Skip version (4), inputcount (1), prevhash (32), vout (4)
    if (coinbase_1_len < coinbase_1_offset) return;

    uint8_t scriptsig_len;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2), &scriptsig_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset) return;
    
    uint8_t block_height_len;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2), &block_height_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset || block_height_len == 0 || block_height_len > 4) return;

    uint32_t block_height = 0;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2), (uint8_t *)&block_height, block_height_len);
    coinbase_1_offset += block_height_len;

    if (block_height != SYSTEM_MODULE.block_height) {
        ESP_LOGI(TAG, "Block height %d", block_height);
        SYSTEM_MODULE.block_height = block_height;
    }

    size_t scriptsig_length = scriptsig_len - 1 - block_height_len - (strlen(MINING_MODULE.extranonce_str) / 2) - MINING_MODULE.extranonce_2_len;
    if (scriptsig_length <= 0) return;
    
    char * scriptsig = malloc(scriptsig_length + 1);

    int coinbase_1_tag_len = coinbase_1_len - coinbase_1_offset;
    hex2bin(mining_notification->coinbase_1 + (coinbase_1_offset * 2), (uint8_t *) scriptsig, coinbase_1_tag_len);

    int coinbase_2_tag_len = scriptsig_length - coinbase_1_tag_len;

    if (coinbase_2_len < coinbase_2_tag_len) return;
    
    if (coinbase_2_tag_len > 0) {
        hex2bin(mining_notification->coinbase_2, (uint8_t *) scriptsig + coinbase_1_tag_len, coinbase_2_tag_len);
    }

    for (int i = 0; i < scriptsig_length; i++) {
        if (!isprint((unsigned char)scriptsig[i])) {
            scriptsig[i] = '.';
        }
    }

    scriptsig[scriptsig_length] = '\0';

    if (SYSTEM_MODULE.scriptsig == NULL || strcmp(scriptsig, SYSTEM_MODULE.scriptsig) != 0) {
        ESP_LOGI(TAG, "Scriptsig: %s", scriptsig);

        char * previous_miner_tag = SYSTEM_MODULE.scriptsig;
        SYSTEM_MODULE.scriptsig = scriptsig;
        free(previous_miner_tag);
    } else {
        free(scriptsig);
    }
}

void stratum_task(void * pvParameters)
{

    primary_stratum_url = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].url;
    primary_stratum_port = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].port;
    char * stratum_url = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].url;
    uint16_t port = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].port;
    bool extranonce_subscribe = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].extranonce_subscribe;
    uint16_t difficulty = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].difficulty;

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
            POOL_MODULE.active_pool_idx = POOL_MODULE.active_pool_idx+1 % 2;
            if (POOL_MODULE.pools[POOL_MODULE.active_pool_idx].url == NULL || POOL_MODULE.pools[POOL_MODULE.active_pool_idx].url[0] == '\0') {
                ESP_LOGI(TAG, "Unable to switch to fallback. No url configured. (retries: %d)...", retry_attempts);
                retry_attempts = 0;
                continue;
            }

            
            // Reset share stats at failover
            for (int i = 0; i < SYSTEM_MODULE.rejected_reason_stats_count; i++) {
                SYSTEM_MODULE.rejected_reason_stats[i].count = 0;
                SYSTEM_MODULE.rejected_reason_stats[i].message[0] = '\0';
            }
            SYSTEM_MODULE.rejected_reason_stats_count = 0;
            SYSTEM_MODULE.shares_accepted = 0;
            SYSTEM_MODULE.shares_rejected = 0;
            SYSTEM_MODULE.work_received = 0;

            ESP_LOGI(TAG, "Switching target due to too many failures (retries: %d)...", retry_attempts);
            retry_attempts = 0;
        }

        stratum_url = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].url;
        port = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].port;
        extranonce_subscribe = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].extranonce_subscribe;
        difficulty = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].difficulty;

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

        sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        if (sock < 0) {
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
        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            retry_attempts++;
            ESP_LOGE(TAG, "Socket unable to connect to %s:%d (errno %d: %s)", stratum_url, port, errno, strerror(errno));
            // close the socket
            shutdown(sock, SHUT_RDWR);
            close(sock);
            // instead of restarting, retry this every 5 seconds
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tcp_snd_timeout, sizeof(tcp_snd_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_SNDTIMEO");
        }

        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO , &tcp_rcv_timeout, sizeof(tcp_rcv_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
        }

        stratum_reset_uid();
        cleanQueue();

        ///// Start Stratum Action
        // mining.configure - ID: 1
        STRATUM_V1_configure_version_rolling(sock, send_uid++, &MINING_MODULE.version_mask);

        // mining.subscribe - ID: 2
        STRATUM_V1_subscribe(sock, send_uid++, DEVICE_CONFIG.family.asic.name);

        char * username = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].user;
        char * password = POOL_MODULE.pools[POOL_MODULE.active_pool_idx].pass;

        int authorize_message_id = send_uid++;
        //mining.authorize - ID: 3
        STRATUM_V1_authorize(sock, authorize_message_id, username, password);
        STRATUM_V1_stamp_tx(authorize_message_id);

        // Everything is set up, lets make sure we don't abandon work unnecessarily.
        MINING_MODULE.abandon_work = 0;

        while (1) {
            char * line = STRATUM_V1_receive_jsonrpc_line(sock);
            if (!line) {
                ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                retry_attempts++;
                stratum_close_connection();
                break;
            }

            double response_time_ms = STRATUM_V1_get_response_time_ms(stratum_api_v1_message.message_id);
            if (response_time_ms >= 0) {
                ESP_LOGI(TAG, "Stratum response time: %.2f ms", response_time_ms);
                POOL_MODULE.response_time = response_time_ms;
            }

            STRATUM_V1_parse(&stratum_api_v1_message, line);
            free(line);

            if (stratum_api_v1_message.method == MINING_NOTIFY) {
                SYSTEM_notify_new_ntime(stratum_api_v1_message.mining_notification->ntime);
                if (stratum_api_v1_message.should_abandon_work &&
                    (MINING_MODULE.stratum_queue.count > 0 || MINING_MODULE.ASIC_jobs_queue.count > 0)) {
                    cleanQueue();
                }
                if (MINING_MODULE.stratum_queue.count == QUEUE_SIZE) {
                    mining_notify * next_notify_json_str = (mining_notify *) queue_dequeue(&MINING_MODULE.stratum_queue);
                    STRATUM_V1_free_mining_notify(next_notify_json_str);
                }
                queue_enqueue(&MINING_MODULE.stratum_queue, stratum_api_v1_message.mining_notification);
                decode_mining_notification(stratum_api_v1_message.mining_notification);
            } else if (stratum_api_v1_message.method == MINING_SET_DIFFICULTY) {
                ESP_LOGI(TAG, "Set pool difficulty: %ld", stratum_api_v1_message.new_difficulty);
                POOL_MODULE.pool_difficulty = stratum_api_v1_message.new_difficulty;
                MINING_MODULE.new_set_mining_difficulty_msg = true;
            } else if (stratum_api_v1_message.method == MINING_SET_VERSION_MASK ||
                    stratum_api_v1_message.method == STRATUM_RESULT_VERSION_MASK) {
                ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
                MINING_MODULE.version_mask = stratum_api_v1_message.version_mask;
                MINING_MODULE.new_stratum_version_rolling_msg = true;
            } else if (stratum_api_v1_message.method == MINING_SET_EXTRANONCE ||
                    stratum_api_v1_message.method == STRATUM_RESULT_SUBSCRIBE) {
                // Validate extranonce_2_len to prevent buffer overflow
                if (stratum_api_v1_message.extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                    ESP_LOGW(TAG, "Extranonce_2_len %d exceeds maximum %d, clamping to maximum", 
                             stratum_api_v1_message.extranonce_2_len, MAX_EXTRANONCE_2_LEN);
                    stratum_api_v1_message.extranonce_2_len = MAX_EXTRANONCE_2_LEN;
                }
                ESP_LOGI(TAG, "Set extranonce: %s, extranonce_2_len: %d", stratum_api_v1_message.extranonce_str, stratum_api_v1_message.extranonce_2_len);
                char * old_extranonce_str = MINING_MODULE.extranonce_str;
                MINING_MODULE.extranonce_str = stratum_api_v1_message.extranonce_str;
                MINING_MODULE.extranonce_2_len = stratum_api_v1_message.extranonce_2_len;
                free(old_extranonce_str);
            } else if (stratum_api_v1_message.method == CLIENT_RECONNECT) {
                ESP_LOGE(TAG, "Pool requested client reconnect...");
                stratum_close_connection();
                break;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT) {
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "message result accepted");
                    SYSTEM_notify_accepted_share();
                } else {
                    ESP_LOGW(TAG, "message result rejected: %s", stratum_api_v1_message.error_str);
                    SYSTEM_notify_rejected_share(stratum_api_v1_message.error_str);
                }
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SETUP) {
                // Reset retry attempts after successfully receiving data.
                retry_attempts = 0;
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "setup message accepted");
                    if (stratum_api_v1_message.message_id == authorize_message_id) {
                        STRATUM_V1_suggest_difficulty(sock, send_uid++, difficulty);
                    }
                    if (extranonce_subscribe) {
                        STRATUM_V1_extranonce_subscribe(sock, send_uid++);
                    }
                } else {
                    ESP_LOGE(TAG, "setup message rejected: %s", stratum_api_v1_message.error_str);
                }
            }
        }
    }
    vTaskDelete(NULL);
}
