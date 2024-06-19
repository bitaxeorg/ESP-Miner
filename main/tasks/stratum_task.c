#include "esp_log.h"
// #include "addr_from_stdin.h"
#include "bm1397.h"
#include "connect.h"
#include "system.h"
#include "global_state.h"
#include "lwip/dns.h"
#include "nvs_config.h"
#include "stratum_task.h"
#include "work_queue.h"
#include "esp_wifi.h"
#include <esp_sntp.h>
#include <time.h>

#define PORT CONFIG_STRATUM_PORT
#define STRATUM_URL CONFIG_STRATUM_URL

#define STRATUM_PW CONFIG_STRATUM_PW
#define STRATUM_DIFFICULTY CONFIG_STRATUM_DIFFICULTY

#define BASE_DELAY_MS 5000
#define MAX_RETRY_ATTEMPTS 5
static const char * TAG = "stratum_task";
static ip_addr_t ip_Addr;
static bool bDNSFound = false;
static bool bDNSInvalid = false;

static StratumApiV1Message stratum_api_v1_message = {};

static SystemTaskModule SYSTEM_TASK_MODULE = {.stratum_difficulty = 8192};

void dns_found_cb(const char * name, const ip_addr_t * ipaddr, void * callback_arg)
{
    if ((ipaddr != NULL)){
        ip4_addr_t ip4addr = ipaddr->u_addr.ip4;  // Obtener la estructura ip4_addr_t
        if (ip4_addr1(&ip4addr) != 0 && ip4_addr2(&ip4addr) != 0 && 
            ip4_addr3(&ip4addr) != 0 && ip4_addr4(&ip4addr) != 0) {
            ESP_LOGI(TAG, "IP found : %d.%d.%d.%d",ip4_addr1(&ip4addr),ip4_addr2(&ip4addr),ip4_addr3(&ip4addr),ip4_addr4(&ip4addr));
            ip_Addr = *ipaddr;
         }
    } else {
        bDNSInvalid = true;
    }
    bDNSFound = true;
 }

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return true;
    } else {
        return false;
    }
}

void stratum_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    STRATUM_V1_initialize_buffer();
    char host_ip[20];
    int addr_family = 0;
    int ip_protocol = 0;
	int retry_attempts = 0;
    int delay_ms = BASE_DELAY_MS;


    char *stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    uint16_t port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;

    while (1) {
        //clear flags used by the dns callback, dns_found_cb()
        bDNSFound = false;
        bDNSInvalid = false;

        // check to see if the STRATUM_URL is an ip address already
        if (inet_pton(AF_INET, stratum_url, &ip_Addr) == 1) {
            bDNSFound = true;
        }
        else
        {
            ESP_LOGI(TAG, "Get IP for URL: %s", stratum_url);
            dns_gethostbyname(stratum_url, &ip_Addr, dns_found_cb, NULL);
            while (!bDNSFound);

            if (bDNSInvalid) {
                ESP_LOGE(TAG, "DNS lookup failed for URL: %s", stratum_url);
                //set ip_Addr to 0.0.0.0 so that connect() will fail
                IP_ADDR4(&ip_Addr, 0, 0, 0, 0);
            }

        }

        // make IP address string from ip_Addr
        snprintf(host_ip, sizeof(host_ip), "%d.%d.%d.%d", ip4_addr1(&ip_Addr.u_addr.ip4), ip4_addr2(&ip_Addr.u_addr.ip4),
                    ip4_addr3(&ip_Addr.u_addr.ip4), ip4_addr4(&ip_Addr.u_addr.ip4));
        ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, host_ip);

        while (1) {
            if (!is_wifi_connected()) {
                ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
                esp_wifi_connect();
                vTaskDelay(10000 / portTICK_PERIOD_MS);
                //delay_ms *= 2; // Increase delay exponentially
                continue;
            }

            struct sockaddr_in dest_addr;
            dest_addr.sin_addr.s_addr = inet_addr(host_ip);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(port);
            addr_family = AF_INET;
            ip_protocol = IPPROTO_IP;

            GLOBAL_STATE->sock = socket(addr_family, SOCK_STREAM, ip_protocol);
            if (GLOBAL_STATE->sock < 0) {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                if (++retry_attempts > MAX_RETRY_ATTEMPTS) {
                    ESP_LOGE(TAG, "Max retry attempts reached, restarting...");
                    esp_restart();
                }
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, port);
            retry_attempts = 0;
            int err = connect(GLOBAL_STATE->sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
            if (err != 0)
            {
                ESP_LOGE(TAG, "Socket unable to connect to %s:%d (errno %d)", stratum_url, port, errno);
                // close the socket
                shutdown(GLOBAL_STATE->sock, SHUT_RDWR);
                close(GLOBAL_STATE->sock);
                // instead of restarting, retry this every 5 seconds
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }

            ///// Start Stratum Action
            // mining.subscribe - ID: 1
            STRATUM_V1_subscribe(GLOBAL_STATE->sock, GLOBAL_STATE->asic_model_str);

            // mining.configure - ID: 2
            STRATUM_V1_configure_version_rolling(GLOBAL_STATE->sock, &GLOBAL_STATE->version_mask);

            //mining.suggest_difficulty - ID: 3
            STRATUM_V1_suggest_difficulty(GLOBAL_STATE->sock, STRATUM_DIFFICULTY);

            char * username = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, STRATUM_USER);
            char * password = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS, STRATUM_PW);

            //mining.authorize - ID: 4
            STRATUM_V1_authenticate(GLOBAL_STATE->sock, username, password);
            free(password);
            free(username);

            while (1) {
                char * line = STRATUM_V1_receive_jsonrpc_line(GLOBAL_STATE->sock);
                if (!line) {
                    ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                    shutdown(GLOBAL_STATE->sock, SHUT_RDWR);
                    close(GLOBAL_STATE->sock);
                    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay before attempting to reconnect
                    break;
                }
                ESP_LOGI(TAG, "rx: %s", line); // debug incoming stratum messages
                STRATUM_V1_parse(&stratum_api_v1_message, line);
                free(line);

                if (stratum_api_v1_message.method == MINING_NOTIFY) {
                    SYSTEM_notify_new_ntime(GLOBAL_STATE, stratum_api_v1_message.mining_notification->ntime);
                    if (stratum_api_v1_message.should_abandon_work &&
                        (GLOBAL_STATE->stratum_queue.count > 0 || GLOBAL_STATE->ASIC_jobs_queue.count > 0)) {
                        ESP_LOGI(TAG, "abandoning work");

                        GLOBAL_STATE->abandon_work = 1;
                        queue_clear(&GLOBAL_STATE->stratum_queue);

                        pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
                        ASIC_jobs_queue_clear(&GLOBAL_STATE->ASIC_jobs_queue);
                        for (int i = 0; i < 128; i = i + 4) {
                            GLOBAL_STATE->valid_jobs[i] = 0;
                        }
                        pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
                    }
                    if (GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
                        mining_notify * next_notify_json_str = (mining_notify *) queue_dequeue(&GLOBAL_STATE->stratum_queue);
                        STRATUM_V1_free_mining_notify(next_notify_json_str);
                    }

                    stratum_api_v1_message.mining_notification->difficulty = SYSTEM_TASK_MODULE.stratum_difficulty;
                    queue_enqueue(&GLOBAL_STATE->stratum_queue, stratum_api_v1_message.mining_notification);
                } else if (stratum_api_v1_message.method == MINING_SET_DIFFICULTY) {
                    if (stratum_api_v1_message.new_difficulty != SYSTEM_TASK_MODULE.stratum_difficulty) {
                        SYSTEM_TASK_MODULE.stratum_difficulty = stratum_api_v1_message.new_difficulty;
                        ESP_LOGI(TAG, "Set stratum difficulty: %ld", SYSTEM_TASK_MODULE.stratum_difficulty);
                    }
                } else if (stratum_api_v1_message.method == MINING_SET_VERSION_MASK ||
                        stratum_api_v1_message.method == STRATUM_RESULT_VERSION_MASK) {
                    // 1fffe000
                    ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
                    GLOBAL_STATE->version_mask = stratum_api_v1_message.version_mask;
                } else if (stratum_api_v1_message.method == STRATUM_RESULT_SUBSCRIBE) {
                    GLOBAL_STATE->extranonce_str = stratum_api_v1_message.extranonce_str;
                    GLOBAL_STATE->extranonce_2_len = stratum_api_v1_message.extranonce_2_len;
                } else if (stratum_api_v1_message.method == STRATUM_RESULT) {
                    if (stratum_api_v1_message.response_success) {
                        ESP_LOGI(TAG, "message result accepted");
                        SYSTEM_notify_accepted_share(GLOBAL_STATE);
                    } else {
                        ESP_LOGE(TAG, "message result rejected");
                        SYSTEM_notify_rejected_share(GLOBAL_STATE);
                    }
                } else if (stratum_api_v1_message.method == STRATUM_RESULT_SETUP) {
                    if (stratum_api_v1_message.response_success) {
                        ESP_LOGI(TAG, "setup message accepted");
                    } else {
                        ESP_LOGE(TAG, "setup message rejected");
                    }
                }
            }

            if (GLOBAL_STATE->sock != -1) {
                ESP_LOGE(TAG, "Shutting down socket and restarting...");
                shutdown(GLOBAL_STATE->sock, 0);
                close(GLOBAL_STATE->sock);
            }
        }
    }
    vTaskDelete(NULL);
}