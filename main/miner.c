#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "addr_from_stdin.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "stratum_api.h"
#include "mining.h"
#include "work_queue.h"
#include "utils.h"

#include "system.h"
#include "serial.h"
#include "bm1397.h"


#define PORT CONFIG_EXAMPLE_STRATUM_PORT
#define STRATUM_URL CONFIG_EXAMPLE_STRATUM_URL
#define STRATUM_USER CONFIG_EXAMPLE_STRATUM_USER
#define STRATUM_PW CONFIG_EXAMPLE_STRATUM_PW



static const char *TAG = "stratum client";

TaskHandle_t sysTaskHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;

static work_queue g_queue;
static work_queue g_bm_queue;

static char * extranonce_str = NULL;
static int extranonce_2_len = 0;

static int sock;

static void AsicTask(void * pvParameters)
{
    init_serial();

    init_BM1397();

    //reset the bm1397
    reset_BM1397();

    //send the init command
    send_read_address();

    //read back response
    debug_serial_rx();

    //send the init commands
    send_init();

    int termination_flag = 0;
    uint8_t buf[CHUNK_SIZE];
    memset(buf, 0, 1024);

    while (1) {
        int id = 1;
        bm_job * next_bm_job = (bm_job *) queue_dequeue(&g_bm_queue, &termination_flag);
        struct job_packet job;
        job.job_id = id++;
        job.num_midstates = 1;
        memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
        memcpy(&job.nbits, &next_bm_job->target, 4);
        memcpy(&job.ntime, &next_bm_job->ntime, 4);
        memcpy(&job.merkle4, &next_bm_job->merkle_root_end, 4);
        memcpy(&job.midstate, &next_bm_job->midstate, 32);

        send_work(&job);
        uint16_t received = serial_rx(buf);
        if (received > 0) {
            ESP_LOGI(TAG, "Received %d bytes from bm1397", received);
            struct nonce_response nonce;
            memcpy((void *) &nonce, buf, sizeof(struct nonce_response));
            //reverse_bytes((uint8_t *) &nonce.nonce, 4);
            print_hex((uint8_t *) &nonce.nonce, 4, 4, "nonce: ");
            memset(buf, 0, 1024);
            //check the nonce difficulty
            if (nonce.nonce < next_bm_job->target) {
                ESP_LOGI(TAG, "Nonce is valid");
            } else {
                ESP_LOGI(TAG, "Nonce is invalid");
            }
            submit_share(sock, STRATUM_USER, next_bm_job->jobid,
                         next_bm_job->ntime, next_bm_job->extranonce2, nonce.nonce);
        }
    }
}

static void mining_task(void * pvParameters)
{
    int termination_flag = 0;
    while (1) {
        char * next_notify_json_str = (char *) queue_dequeue(&g_queue, &termination_flag);
        ESP_LOGI(TAG, "New Work Dequeued");
        ESP_LOGI(TAG, "Notify json: %s", next_notify_json_str);
        uint32_t free_heap_size = esp_get_free_heap_size();
        ESP_LOGI(TAG, "miner heap free size: %u bytes", free_heap_size);

        mining_notify params = parse_mining_notify_message(next_notify_json_str);

        char extranonce_2[extranonce_2_len * 2 + 1];
        memset(extranonce_2, '9', extranonce_2_len * 2);
        extranonce_2[extranonce_2_len * 2] = '\0';

        char * coinbase_tx = construct_coinbase_tx(params.coinbase_1, params.coinbase_2,
                                                   extranonce_str, extranonce_2);
        ESP_LOGI(TAG, "Coinbase tx: %s", coinbase_tx);

        char * merkle_root = calculate_merkle_root_hash(coinbase_tx,
                                                        (uint8_t(*)[32]) params.merkle_branches,
                                                        params.n_merkle_branches);
        ESP_LOGI(TAG, "Merkle root: %s", merkle_root);

        bm_job next_job = construct_bm_job(params.version, params.prev_block_hash, merkle_root,
                                           params.ntime, params.target);
        
        ESP_LOGI(TAG, "bm_job: ");
        // print_hex((uint8_t *) &next_job.target, 4, 4, "nbits: ");
        // print_hex((uint8_t *) &next_job.ntime, 4, 4, "ntime: ");
        // print_hex((uint8_t *) &next_job.merkle_root_end, 4, 4, "merkle root end: ");
        print_hex(next_job.midstate, 32, 32, "midstate: ");

        bm_job * queued_next_job = malloc(sizeof(bm_job));
        memcpy(queued_next_job, &next_job, sizeof(bm_job));
        queued_next_job->extranonce2 = strdup(extranonce_2);
        queued_next_job->jobid = strdup(params.job_id);
        queue_enqueue(&g_bm_queue, queued_next_job);

        free_mining_notify(params);
        free(coinbase_tx);
        free(merkle_root);
        free(next_notify_json_str);
    }
}

    ip_addr_t ip_Addr;
    bool bDNSFound = false;

void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    ip_Addr = *ipaddr;
    bDNSFound = true;
}

static void admin_task(void *pvParameters)
{
    initialize_stratum_buffer();
    char host_ip[20];
    int addr_family = 0;
    int ip_protocol = 0;

    //get ip address from hostname

    IP_ADDR4( &ip_Addr, 0,0,0,0 );
    printf("Get IP for URL: %s\n", STRATUM_URL);
    dns_gethostbyname(STRATUM_URL, &ip_Addr, dns_found_cb, NULL );
    while( !bDNSFound );

    //make IP address string from ip_Addr
    snprintf( host_ip, sizeof(host_ip), "%d.%d.%d.%d", 
        ip4_addr1(&ip_Addr.u_addr.ip4), 
        ip4_addr2(&ip_Addr.u_addr.ip4), 
        ip4_addr3(&ip_Addr.u_addr.ip4), 
        ip4_addr4(&ip_Addr.u_addr.ip4) );
    printf( "Connecting to: stratum+tcp://%s:%d (%s)\n", STRATUM_URL, PORT, host_ip);
        
    // printf( "DNS found: %i.%i.%i.%i\n", 
    //     ip4_addr1(&ip_Addr.u_addr.ip4), 
    //     ip4_addr2(&ip_Addr.u_addr.ip4), 
    //     ip4_addr3(&ip_Addr.u_addr.ip4), 
    //     ip4_addr4(&ip_Addr.u_addr.ip4) );

    while (1) {

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;

        sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        subscribe_to_stratum(sock, &extranonce_str, &extranonce_2_len);

        auth_to_stratum(sock, STRATUM_USER);

        ESP_LOGI(TAG, "Extranonce: %s", extranonce_str);
        ESP_LOGI(TAG, "Extranonce 2 length: %d", extranonce_2_len);

        while (1)
        {
            char * line = receive_jsonrpc_line(sock);
            ESP_LOGI(TAG, "Received line: %s", line);
            ESP_LOGI(TAG, "Received line length: %d", strlen(line));

            stratum_method method = parse_stratum_method(line);
            if (method == MINING_NOTIFY) {
                if (should_abandon_work(line)) {
                    queue_clear(&g_queue);
                }
                queue_enqueue(&g_queue, line);
            } else {
                free(line);
            }
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe!");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(SysTask, "System_Task", 4096, NULL, 10, &sysTaskHandle);

    queue_init(&g_queue);
    queue_init(&g_bm_queue);

    xTaskCreate(admin_task, "stratum admin", 8192, NULL, 5, NULL);
    xTaskCreate(mining_task, "stratum miner", 8192, NULL, 5, NULL);
    xTaskCreate(AsicTask, "asic", 4096, NULL, 10, &serialTaskHandle);
}
