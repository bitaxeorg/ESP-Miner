#include "esp_log.h"
#include "system.h"
#include "global_state.h"
#include <lwip/tcpip.h>
#include <lwip/netdb.h>
#include "stratum_task.h"
#include "work_queue.h"
#include "esp_wifi.h"
#include <esp_sntp.h>
#include <sys/time.h>
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "coinbase_decoder.h"
#include <esp_heap_caps.h>

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

static const char * TAG = "stratum_task";

static StratumApiV1Message stratum_api_v1_message = {};

static const char * primary_stratum_url;
static uint16_t primary_stratum_port;

struct timeval tcp_snd_timeout = {
    .tv_sec = 5,
    .tv_usec = 0
};

struct timeval tcp_rcv_timeout = {
    .tv_sec = 60 * 3,
    .tv_usec = 0
};

static uint16_t primary_stratum_tls;
static char * primary_stratum_cert;

typedef struct {
    struct sockaddr_storage dest_addr;  // Stores IPv4 or IPv6 address with scope_id for IPv6
    socklen_t addrlen;
    int addr_family;
    int ip_protocol;
    char host_ip[INET6_ADDRSTRLEN + 16];  // IPv6 address + zone identifier (e.g., "fe80::1%wlan0")
} stratum_connection_info_t;

static esp_err_t resolve_stratum_address(const char *hostname, uint16_t port, stratum_connection_info_t *conn_info)
{
    // Input validation
    if (hostname == NULL || conn_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (port == 0) {
        ESP_LOGE(TAG, "Invalid port: 0");
        return ESP_ERR_INVALID_ARG;
    }

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    ESP_LOGD(TAG, "Resolving address for %s:%u", hostname, port);

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags    = AI_NUMERICSERV
    };

    struct addrinfo *res = NULL;
    int gai_err = esp_getaddrinfo(hostname, port_str, &hints, &res);
    if (gai_err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS resolution failed for %s:%u (error: %d)", hostname, port, gai_err);
        return ESP_ERR_NOT_FOUND;
    }

    // Initialize connection info
    memset(conn_info, 0, sizeof(*conn_info));
    conn_info->addr_family = AF_UNSPEC;

    // Preferred order: IPv4 first, then IPv6
    const int preferred_families[] = { AF_INET, AF_INET6 };
    const size_t num_families = sizeof(preferred_families) / sizeof(preferred_families[0]);

    const struct addrinfo *selected = NULL;

    for (size_t i = 0; i < num_families && selected == NULL; i++) {
        int family = preferred_families[i];

        for (const struct addrinfo *p = res; p != NULL; p = p->ai_next) {
            if (p->ai_family == family) {
                selected = p;
                break;
            }
        }
    }

    if (selected == NULL) {
        ESP_LOGE(TAG, "No supported address family (IPv4 or IPv6) found for %s", hostname);
        freeaddrinfo(res);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Copy selected address
    memcpy(&conn_info->dest_addr, selected->ai_addr, selected->ai_addrlen);
    conn_info->addrlen     = selected->ai_addrlen;
    conn_info->addr_family = selected->ai_family;
    conn_info->ip_protocol = (selected->ai_family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;

    // Handle IPv6 link-local scope ID if needed
    if (selected->ai_family == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;

        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
            if (addr6->sin6_scope_id == 0) {
                ESP_LOGW(TAG, "Link-local IPv6 address without scope ID - attempting to set from WiFi STA interface");

                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif) {
                    int index = esp_netif_get_netif_impl_index(netif);
                    if (index >= 0) {
                        addr6->sin6_scope_id = (uint32_t)index;
                        ESP_LOGI(TAG, "Set IPv6 scope_id to interface index: %lu", (unsigned long)addr6->sin6_scope_id);
                    } else {
                        ESP_LOGW(TAG, "Failed to get valid interface index for WIFI_STA_DEF");
                    }
                } else {
                    ESP_LOGW(TAG, "Could not get netif handle for WIFI_STA_DEF");
                }
            } else {
                ESP_LOGI(TAG, "Link-local IPv6 address with existing scope_id: %lu", (unsigned long)addr6->sin6_scope_id);
            }
        }
    }

    // Convert resolved address to string for logging and storage
    const void *src_addr;
    int af = conn_info->addr_family;

    if (af == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&conn_info->dest_addr;
        src_addr = &addr4->sin_addr;
    } else {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
        src_addr = &addr6->sin6_addr;
    }

    if (inet_ntop(af, src_addr, conn_info->host_ip, sizeof(conn_info->host_ip)) == NULL) {
        ESP_LOGW(TAG, "inet_ntop failed (errno: %d)", errno);
        snprintf(conn_info->host_ip, sizeof(conn_info->host_ip), "[invalid %s addr]",
                 (af == AF_INET) ? "IPv4" : "IPv6");
    } else if (af == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr) && addr6->sin6_scope_id != 0) {
            char zone[16];
            snprintf(zone, sizeof(zone), "%%%lu", (unsigned long)addr6->sin6_scope_id);
            strncat(conn_info->host_ip, zone,
                    sizeof(conn_info->host_ip) - strlen(conn_info->host_ip) - 1);
            // Ensure null termination
            conn_info->host_ip[sizeof(conn_info->host_ip) - 1] = '\0';
        }
    }

    ESP_LOGI(TAG, "Resolved %s:%u → %s", hostname, port, conn_info->host_ip);

    freeaddrinfo(res);
    return ESP_OK;
}

static void set_socket_options(esp_transport_handle_t transport)
{
    int sock = esp_transport_get_socket(transport);
    if (sock >= 0) {
        // Set send and receive timeouts
        if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tcp_snd_timeout, sizeof(tcp_snd_timeout)) < 0) {
            ESP_LOGE(TAG, "Failed to set SO_SNDTIMEO");
        }
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tcp_rcv_timeout, sizeof(tcp_rcv_timeout)) < 0) {
            ESP_LOGE(TAG, "Failed to set SO_RCVTIMEO");
        }

        // Enable keepalive
        int keepalive = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
            ESP_LOGE(TAG, "Failed to set SO_KEEPALIVE");
        }

        // Set keepalive parameters (adjust values as needed)
        int keepidle = 60;  // TCP_KEEPIDLE: seconds before sending keepalive
        int keepintvl = 10; // TCP_KEEPINTVL: seconds between keepalive probes
        int keepcnt = 3;    // TCP_KEEPCNT: number of keepalive probes
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
            ESP_LOGE(TAG, "Failed to set TCP_KEEPIDLE");
        }
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
            ESP_LOGE(TAG, "Failed to set TCP_KEEPINTVL");
        }
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
            ESP_LOGE(TAG, "Failed to set TCP_KEEPCNT");
        }
    } else {
        ESP_LOGE(TAG, "Failed to get socket from transport");
    }
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
    queue_clear(&GLOBAL_STATE->stratum_queue);

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
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
    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    esp_transport_close(GLOBAL_STATE->transport);
    GLOBAL_STATE->transport = NULL;
    cleanQueue(GLOBAL_STATE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void stratum_primary_heartbeat(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_LOGI(TAG, "Starting heartbeat thread for primary pool: %s:%d", primary_stratum_url, primary_stratum_port);
    vTaskDelay(10000 / portTICK_PERIOD_MS);

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
       
        tls_mode tls = GLOBAL_STATE->SYSTEM_MODULE.pool_tls;
        char * cert = GLOBAL_STATE->SYSTEM_MODULE.pool_cert;
        esp_transport_handle_t transport = STRATUM_V1_transport_init(tls, cert);
        if (transport == NULL) {
            ESP_LOGD(TAG, "Heartbeat. Failed transport init check!");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        esp_err_t err = esp_transport_connect(transport, primary_stratum_url, primary_stratum_port, TRANSPORT_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "Heartbeat. Failed connect check: %s:%d (errno %d: %s)", primary_stratum_url, primary_stratum_port, err, strerror(err));
            esp_transport_close(transport);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        set_socket_options(transport);

        int send_uid = 1;
        STRATUM_V1_subscribe(transport, send_uid++, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_authorize(transport, send_uid++, GLOBAL_STATE->SYSTEM_MODULE.pool_user, GLOBAL_STATE->SYSTEM_MODULE.pool_pass);

        char recv_buffer[BUFFER_SIZE];
        memset(recv_buffer, 0, BUFFER_SIZE);
        int bytes_received = esp_transport_read(transport, recv_buffer, BUFFER_SIZE - 1, TRANSPORT_TIMEOUT_MS); 

        esp_transport_close(transport);

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
    mining_notification_result_t *result = heap_caps_malloc(sizeof(mining_notification_result_t), MALLOC_CAP_SPIRAM);
    if (!result) {
        ESP_LOGE(TAG, "Failed to allocate result in PSRAM");
        return;
    }
    memset(result, 0, sizeof(mining_notification_result_t));

    const char * user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE->SYSTEM_MODULE.pool_user;

    if (coinbase_process_notification(mining_notification,
                                     GLOBAL_STATE->extranonce_str,
                                     GLOBAL_STATE->extranonce_2_len,
                                     user,
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
    if (result->output_count != GLOBAL_STATE->coinbase_output_count ||
        memcmp(result->outputs, GLOBAL_STATE->coinbase_outputs, sizeof(coinbase_output_t) * result->output_count) != 0) {

        GLOBAL_STATE->coinbase_output_count = result->output_count;
        memcpy(GLOBAL_STATE->coinbase_outputs, result->outputs, sizeof(coinbase_output_t) * result->output_count);
        GLOBAL_STATE->coinbase_value_total_satoshis = result->total_value_satoshis;
        GLOBAL_STATE->coinbase_value_user_satoshis = result->user_value_satoshis;
        ESP_LOGI(TAG, "Coinbase outputs: %d, total value: %llu sat", result->output_count, result->total_value_satoshis);
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

void stratum_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    primary_stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    primary_stratum_port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;
    primary_stratum_tls = GLOBAL_STATE->SYSTEM_MODULE.pool_tls;
    primary_stratum_cert = GLOBAL_STATE->SYSTEM_MODULE.pool_cert;
    char * stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    uint16_t port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;
    bool extranonce_subscribe = GLOBAL_STATE->SYSTEM_MODULE.pool_extranonce_subscribe;
    uint16_t difficulty = GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty;
    tls_mode tls = GLOBAL_STATE->SYSTEM_MODULE.pool_tls;
    char * cert = GLOBAL_STATE->SYSTEM_MODULE.pool_cert;

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

        tls = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_tls : GLOBAL_STATE->SYSTEM_MODULE.pool_tls;
        cert = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_cert : GLOBAL_STATE->SYSTEM_MODULE.pool_cert;
        retry_critical_attempts = 0;

        GLOBAL_STATE->transport = STRATUM_V1_transport_init(tls, cert);
        // Check if transport was initialized
        if(GLOBAL_STATE->transport == NULL) {
            ESP_LOGE(TAG, "Transport initialization failed.");
            if (++retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
                ESP_LOGE(TAG, "Max retry attempts reached, restarting...");
                esp_restart();
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        retry_critical_attempts = 0;

        ESP_LOGI(TAG, "Transport initialized, connecting to %s:%d", stratum_url, port);
        esp_err_t ret = esp_transport_connect(GLOBAL_STATE->transport, stratum_url, port, TRANSPORT_TIMEOUT_MS);
        if (ret != ESP_OK) {
            retry_attempts ++;
            ESP_LOGE(TAG, "Transport unable to connect to %s:%d (errno %d). Attempt: %d", stratum_url, port, ret, retry_attempts);
            // close the transport
            esp_transport_close(GLOBAL_STATE->transport);
            // instead of restarting, retry this every 5 seconds
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        set_socket_options(GLOBAL_STATE->transport);

        const char* protocol = (conn_info.addr_family == AF_INET6) ? "IPv6" : "IPv4";
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

        stratum_reset_uid(GLOBAL_STATE);
        cleanQueue(GLOBAL_STATE);

        ///// Start Stratum Action
        // mining.configure - ID: 1
        STRATUM_V1_configure_version_rolling(GLOBAL_STATE->transport, GLOBAL_STATE->send_uid++, &GLOBAL_STATE->version_mask);

        // mining.subscribe - ID: 2
        STRATUM_V1_subscribe(GLOBAL_STATE->transport, GLOBAL_STATE->send_uid++, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);

        char * username = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
        char * password = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_pass : GLOBAL_STATE->SYSTEM_MODULE.pool_pass;

        int authorize_message_id = GLOBAL_STATE->send_uid++;

        //mining.authorize - ID: 3
        STRATUM_V1_authorize(GLOBAL_STATE->transport, authorize_message_id, username, password);
        STRATUM_V1_stamp_tx(authorize_message_id);

        while (1) {
            char * line = STRATUM_V1_receive_jsonrpc_line(GLOBAL_STATE->transport);
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
                if (stratum_api_v1_message.mining_notification->clean_jobs &&
                    (GLOBAL_STATE->stratum_queue.count > 0)) {
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
            } else if (stratum_api_v1_message.method == MINING_PING) { 
                STRATUM_V1_pong(GLOBAL_STATE->transport, stratum_api_v1_message.message_id);
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
                        STRATUM_V1_suggest_difficulty(GLOBAL_STATE->transport, GLOBAL_STATE->send_uid++, difficulty);
                    }
                    if (extranonce_subscribe) {
                        STRATUM_V1_extranonce_subscribe(GLOBAL_STATE->transport, GLOBAL_STATE->send_uid++);
                    }
                } else {
                    ESP_LOGE(TAG, "setup message rejected: %s", stratum_api_v1_message.error_str);
                }
            }
        }
    }
    vTaskDelete(NULL);
}
