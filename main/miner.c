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
#include "stratum_api.h"
#include "mining.h"
#include "work_queue.h"

#include "system.h"
#include "serial.h"

#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#elif defined(CONFIG_EXAMPLE_IPV6)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#else
#define HOST_IP_ADDR ""
#endif

#define PORT CONFIG_EXAMPLE_PORT

#define STRATUM_USERNAME "johnny9.esp"


static const char *TAG = "stratum client";

TaskHandle_t sysTaskHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;

static work_queue g_queue;

static char * extranonce_str = NULL;
static int extranonce_2_len = 0;

static void mining_task(void *pvParameters)
{
    int termination_flag = 0;
    while(true) {
        char * next_notify_json_str = queue_dequeue(&g_queue, &termination_flag);
        ESP_LOGI(TAG, "New Work Dequeued");
        ESP_LOGI(TAG, "Notify json: %s", next_notify_json_str);
        uint32_t free_heap_size = esp_get_free_heap_size();
        ESP_LOGI(TAG, "miner heap free size: %lu bytes", free_heap_size);

        mining_notify params = parse_mining_notify_message(next_notify_json_str);

        char * coinbase_tx = construct_coinbase_tx(params.coinbase_1, params.coinbase_2,
                                                   extranonce_str, extranonce_2_len);
        ESP_LOGI(TAG, "Coinbase tx: %s", coinbase_tx);

        char * merkle_root = calculate_merkle_root_hash(coinbase_tx,
                                                        (uint8_t(*)[32]) params.merkle_branches,
                                                        params.n_merkle_branches);
        ESP_LOGI(TAG, "Merkle root: %s", merkle_root);

        free_mining_notify(params);
        free(coinbase_tx);
        free(merkle_root);
        free(next_notify_json_str);

        // TODO: Prepare the block header and start mining

        // TODO: Increment the nonce

        // TODO: Update the block header with the new nonce
        // TODO: Hash the block header using SHA256 twice (double SHA256)
        // TODO: Check if the hash meets the target specified by nbits

        // TODO: If hash meets target, submit shares
        // snprintf(submit_msg, BUFFER_SIZE,
        //          "{\"id\": 4, \"method\": \"mining.submit\", \"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%08x\"]}\n",
        //          user, job_id, ntime, extranonce2, nonce);
    }
}

static void admin_task(void *pvParameters)
{
    initialize_stratum_buffer();
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    while (1)
    {
#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_IPV6)
        struct sockaddr_in6 dest_addr = {0};
        inet6_aton(host_ip, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = {0};
        ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_STREAM, &ip_protocol, &addr_family, &dest_addr));
#endif
        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
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

        auth_to_stratum(sock, STRATUM_USERNAME);

        subscribe_to_stratum(sock, &extranonce_str, &extranonce_2_len);
        ESP_LOGI(TAG, "Extranonce: %s", extranonce_str);
        ESP_LOGI(TAG, "Extranonce 2 length: %d", extranonce_2_len);

        while (1)
        {
            char * line = receive_jsonrpc_line(sock);
            ESP_LOGI(TAG, "Received line: %s", line);
            ESP_LOGI(TAG, "Received line length: %d", strlen(line));

            stratum_method method = parse_stratum_method(line);
            if (method == MINING_NOTIFY) {
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
    xTaskCreate(SerialTask, "serial_test", 4096, NULL, 10, &serialTaskHandle);

    queue_init(&g_queue);

    xTaskCreate(admin_task, "stratum admin", 8192, NULL, 5, NULL);
    xTaskCreate(mining_task, "stratum miner", 8192, NULL, 5, NULL);
}
