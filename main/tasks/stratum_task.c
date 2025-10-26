#include "esp_log.h"
#include "connect.h"
#include "system.h"
#include "global_state.h"
#include "lwip/dns.h"
#include <lwip/tcpip.h>
#include <lwip/netdb.h>
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

typedef struct {
    struct sockaddr_storage dest_addr;  // Stores IPv4 or IPv6 address with scope_id for IPv6
    socklen_t addrlen;
    int addr_family;
    int ip_protocol;
    char host_ip[INET6_ADDRSTRLEN + 16];  // IPv6 address + zone identifier (e.g., "fe80::1%wlan0")
} stratum_connection_info_t;

static esp_err_t resolve_stratum_address(const char *hostname, uint16_t port, stratum_connection_info_t *conn_info)
{
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags = AI_NUMERICSERV  // Port is numeric
    };
    struct addrinfo *res;
    char port_str[6];
    
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    ESP_LOGD(TAG, "Resolving address for hostname: %s (port %d)", hostname, port);
    
    int gai_err = getaddrinfo(hostname, port_str, &hints, &res);
    if (gai_err != 0) {
        ESP_LOGE(TAG, "getaddrinfo failed for %s: error code %d", hostname, gai_err);
        return ESP_FAIL;
    }

    memset(conn_info, 0, sizeof(stratum_connection_info_t));
    conn_info->addr_family = AF_UNSPEC;

    // Prefer IPv6
    struct addrinfo *p;
    for (p = res; p != NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET6) {
            memcpy(&conn_info->dest_addr, p->ai_addr, p->ai_addrlen);
            conn_info->addrlen = p->ai_addrlen;
            conn_info->addr_family = AF_INET6;
            conn_info->ip_protocol = IPPROTO_IPV6;
            
            // Log scope ID for IPv6 link-local addresses
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
            if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
                ESP_LOGI(TAG, "Link-local IPv6 address detected, scope_id: %lu", (unsigned long)addr6->sin6_scope_id);
                if (addr6->sin6_scope_id == 0) {
                    ESP_LOGW(TAG, "Warning: Link-local IPv6 without scope ID - attempting to set from WIFI_STA_DEF");
                    // Try to get the WiFi STA interface index
                    esp_netif_t *esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                    if (esp_netif) {
                        int netif_index = esp_netif_get_netif_impl_index(esp_netif);
                        if (netif_index >= 0) {
                            addr6->sin6_scope_id = (u32_t)netif_index;
                            ESP_LOGI(TAG, "Set scope_id to interface index: %lu", (unsigned long)addr6->sin6_scope_id);
                        }
                    }
                }
            }
            break;
        }
    }

    // If no IPv6, use IPv4
    if (conn_info->addr_family == AF_UNSPEC) {
        for (p = res; p != NULL; p = p->ai_next) {
            if (p->ai_family == AF_INET) {
                memcpy(&conn_info->dest_addr, p->ai_addr, p->ai_addrlen);
                conn_info->addrlen = p->ai_addrlen;
                conn_info->addr_family = AF_INET;
                conn_info->ip_protocol = IPPROTO_IP;
                break;
            }
        }
    }

    freeaddrinfo(res);

    if (conn_info->addr_family == AF_UNSPEC) {
        ESP_LOGE(TAG, "No suitable address found for %s", hostname);
        return ESP_FAIL;
    }

    // Convert address to string for logging
    if (conn_info->addr_family == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
        inet_ntop(AF_INET6, &addr6->sin6_addr,
                  conn_info->host_ip, sizeof(conn_info->host_ip));
        
        // Append zone identifier for link-local addresses
        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr) && addr6->sin6_scope_id != 0) {
            char zone_buf[16];
            snprintf(zone_buf, sizeof(zone_buf), "%%%lu", addr6->sin6_scope_id);
            strncat(conn_info->host_ip, zone_buf, sizeof(conn_info->host_ip) - strlen(conn_info->host_ip) - 1);
        }
    } else {
        inet_ntop(AF_INET, &((struct sockaddr_in *)&conn_info->dest_addr)->sin_addr,
                  conn_info->host_ip, sizeof(conn_info->host_ip));
    }

    return ESP_OK;
}

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

        ESP_LOGD(TAG, "Running Heartbeat on: %s!", primary_stratum_url);

        if (!is_wifi_connected()) {
            ESP_LOGD(TAG, "Heartbeat. Failed WiFi check!");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        stratum_connection_info_t conn_info;
        if (resolve_stratum_address(primary_stratum_url, primary_stratum_port, &conn_info) != ESP_OK) {
            ESP_LOGD(TAG, "Heartbeat. Address resolution failed for: %s", primary_stratum_url);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        int sock = socket(conn_info.addr_family, SOCK_STREAM, conn_info.ip_protocol);
        if (sock < 0) {
            ESP_LOGD(TAG, "Heartbeat. Failed socket create check!");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        int err = connect(sock, (struct sockaddr *)&conn_info.dest_addr, conn_info.addrlen);
        if (err != 0)
        {
            ESP_LOGD(TAG, "Heartbeat. Failed connect check: %s:%d (errno %d: %s)", conn_info.host_ip, primary_stratum_port, errno, strerror(errno));
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

        if (strstr(recv_buffer, "mining.notify") != NULL && !GLOBAL_STATE->SYSTEM_MODULE.use_fallback_stratum) {
            ESP_LOGI(TAG, "Heartbeat successful and in fallback mode. Switching back to primary.");
            GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = false;
            stratum_close_connection(GLOBAL_STATE);
            continue;
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

static void decode_mining_notification(GlobalState * GLOBAL_STATE, const mining_notify *mining_notification)
{
    double network_difficulty = networkDifficulty(mining_notification->target);
    suffixString(network_difficulty, GLOBAL_STATE->network_diff_string, DIFF_STRING_SIZE, 0);    

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

    if (block_height != GLOBAL_STATE->block_height) {
        ESP_LOGI(TAG, "Block height %d", block_height);
        GLOBAL_STATE->block_height = block_height;
    }

    size_t scriptsig_length = scriptsig_len - 1 - block_height_len;
    if (coinbase_1_len - coinbase_1_offset < scriptsig_len - 1 - block_height_len) {
        scriptsig_length -= (strlen(GLOBAL_STATE->extranonce_str) / 2) + GLOBAL_STATE->extranonce_2_len;
    }
    if (scriptsig_length <= 0) return;
    
    char * scriptsig = malloc(scriptsig_length + 1);
    if (!scriptsig) return;

    int coinbase_1_tag_len = coinbase_1_len - coinbase_1_offset;
    if (coinbase_1_tag_len > scriptsig_length) {
        coinbase_1_tag_len = scriptsig_length;
    }

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

    if (GLOBAL_STATE->scriptsig == NULL || strcmp(scriptsig, GLOBAL_STATE->scriptsig) != 0) {
        ESP_LOGI(TAG, "Scriptsig: %s", scriptsig);

        char * previous_miner_tag = GLOBAL_STATE->scriptsig;
        GLOBAL_STATE->scriptsig = scriptsig;
        free(previous_miner_tag);
    } else {
        free(scriptsig);
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
    int retry_attempts = 0;
    int retry_critical_attempts = 0;

    xTaskCreateWithCaps(stratum_primary_heartbeat, "stratum primary heartbeat", 8192, pvParameters, 1, NULL, MALLOC_CAP_SPIRAM);

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
            GLOBAL_STATE->SYSTEM_MODULE.work_received = 0;

            ESP_LOGI(TAG, "Switching target due to too many failures (retries: %d)...", retry_attempts);
            retry_attempts = 0;
        }

        stratum_url = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url : GLOBAL_STATE->SYSTEM_MODULE.pool_url;
        port = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_port : GLOBAL_STATE->SYSTEM_MODULE.pool_port;
        extranonce_subscribe = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_extranonce_subscribe : GLOBAL_STATE->SYSTEM_MODULE.pool_extranonce_subscribe;
        difficulty = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_difficulty : GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty;

        stratum_connection_info_t conn_info;
        if (resolve_stratum_address(stratum_url, port, &conn_info) != ESP_OK) {
            ESP_LOGE(TAG, "Address resolution failed for %s", stratum_url);
            retry_attempts++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, conn_info.host_ip);

        GLOBAL_STATE->sock = socket(conn_info.addr_family, SOCK_STREAM, conn_info.ip_protocol);
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

        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", conn_info.host_ip, port);
        int err = connect(GLOBAL_STATE->sock, (struct sockaddr *)&conn_info.dest_addr, conn_info.addrlen);
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
                GLOBAL_STATE->SYSTEM_MODULE.work_received++;
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
                decode_mining_notification(GLOBAL_STATE, stratum_api_v1_message.mining_notification);
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
                    if (stratum_api_v1_message.message_id == authorize_message_id && difficulty > 0) {
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
