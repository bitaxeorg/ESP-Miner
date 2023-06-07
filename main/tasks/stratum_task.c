
#include "esp_log.h"
#include "addr_from_stdin.h"
#include "lwip/dns.h"
#include "work_queue.h"
#include "bm1397.h"
#include "global_state.h"

#define PORT CONFIG_STRATUM_PORT
#define STRATUM_URL CONFIG_STRATUM_URL

#define STRATUM_PW CONFIG_STRATUM_PW
#define STRATUM_DIFFICULTY CONFIG_STRATUM_DIFFICULTY

static const char *TAG = "stratum_task";
static ip_addr_t ip_Addr;
static bool bDNSFound = false;
static bool difficulty_changed = false;


void dns_found_cb(const char * name, const ip_addr_t * ipaddr, void * callback_arg)
{
    ip_Addr = *ipaddr;
    bDNSFound = true;
}


void stratum_task(void * pvParameters)
{

    GlobalState *GLOBAL_STATE = (GlobalState*)pvParameters;

    initialize_stratum_buffer();
    char host_ip[20];
    int addr_family = 0;
    int ip_protocol = 0;

    //get ip address from hostname
    IP_ADDR4(&ip_Addr, 0, 0, 0, 0);
    printf("Get IP for URL: %s\n", STRATUM_URL);
    dns_gethostbyname(STRATUM_URL, &ip_Addr, dns_found_cb, NULL);
    while (!bDNSFound);

    //make IP address string from ip_Addr
    snprintf(host_ip, sizeof(host_ip), "%d.%d.%d.%d",
             ip4_addr1(&ip_Addr.u_addr.ip4),
             ip4_addr2(&ip_Addr.u_addr.ip4),
             ip4_addr3(&ip_Addr.u_addr.ip4),
             ip4_addr4(&ip_Addr.u_addr.ip4));
    printf("Connecting to: stratum+tcp://%s:%d (%s)\n", STRATUM_URL, PORT, host_ip);

    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;

        GLOBAL_STATE->sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (GLOBAL_STATE->sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        int err = connect(GLOBAL_STATE->sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }

        uint32_t version_mask = 0;

        subscribe_to_stratum(GLOBAL_STATE->sock, &GLOBAL_STATE->extranonce_str, &GLOBAL_STATE->extranonce_2_len);

        configure_version_rolling(GLOBAL_STATE->sock);

        auth_to_stratum(GLOBAL_STATE->sock, STRATUM_USER);

        ESP_LOGI(TAG, "Extranonce: %s", GLOBAL_STATE->extranonce_str);
        ESP_LOGI(TAG, "Extranonce 2 length: %d", GLOBAL_STATE->extranonce_2_len);

        suggest_difficulty(GLOBAL_STATE->sock, STRATUM_DIFFICULTY);

        while (1)
        {
            char * line = receive_jsonrpc_line( GLOBAL_STATE->sock);
            ESP_LOGI(TAG, "stratum rx: %s", line); //debug incoming stratum messages

            stratum_method method = parse_stratum_method(line);

            if (method == MINING_NOTIFY) {
                if ((difficulty_changed || should_abandon_work(line)) && GLOBAL_STATE->stratum_queue.count > 0) {

                    if (difficulty_changed) {
                        ESP_LOGI(TAG, "pool diff changed, clearing queues");
                        difficulty_changed = false;
                    } else {
                        ESP_LOGI(TAG, "clean_jobs is true, clearing queues");
                    }
                    
                    GLOBAL_STATE->abandon_work = 1;
                    queue_clear(&GLOBAL_STATE->stratum_queue);

                    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
                    ASIC_jobs_queue_clear(&GLOBAL_STATE->ASIC_jobs_queue);
                    for (int i = 0; i < 128; i = i + 4) {
                         GLOBAL_STATE->valid_jobs[i] = 0;
                    }
                    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
                }
                if ( GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
                    char * next_notify_json_str = (char *) queue_dequeue(&GLOBAL_STATE->stratum_queue);
                    free(next_notify_json_str);
                }
                queue_enqueue(&GLOBAL_STATE->stratum_queue, line);
            } else if (method == MINING_SET_DIFFICULTY) {
                uint32_t new_difficulty = parse_mining_set_difficulty_message(line);
                if (new_difficulty != GLOBAL_STATE->stratum_difficulty) {
                    GLOBAL_STATE->stratum_difficulty = new_difficulty;
                    difficulty_changed = true;
                    ESP_LOGI(TAG, "Set stratum difficulty: %d", GLOBAL_STATE->stratum_difficulty);
                    BM1397_set_job_difficulty_mask(GLOBAL_STATE->stratum_difficulty);
                }
                free(line);

            } else if (method == MINING_SET_VERSION_MASK) {
                version_mask = parse_mining_set_version_mask_message(line);

                //1fffe000
                ESP_LOGI(TAG, "Set version mask: %08x", version_mask);
                free(line);

            } else if (method == STRATUM_RESULT) {
                int16_t parsed_id;
                if (parse_stratum_result_message(line, &parsed_id)) {
                    ESP_LOGI(TAG, "message id %d result accepted", parsed_id);
                    SYSTEM_notify_accepted_share(&GLOBAL_STATE->SYSTEM_MODULE);
                } else {
                    ESP_LOGI(TAG, "message id %d result rejected", parsed_id);
                    SYSTEM_notify_rejected_share(&GLOBAL_STATE->SYSTEM_MODULE);
                }
                free(line);
            }  else {
                free(line);
            }
               
            
        }

        if (GLOBAL_STATE->sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(GLOBAL_STATE->sock, 0);
            close(GLOBAL_STATE->sock);
        }
    }
    vTaskDelete(NULL);
}
