/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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

static const char *TAG = "stratum client";

TaskHandle_t sysTaskHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;

static work_queue g_queue;

static void mining_task(void *pvParameters)
{
    int termination_flag = 0;
    while(true) {
        work next_work = queue_dequeue(&g_queue, &termination_flag);
        ESP_LOGI(TAG, "New Work Dequeued");
        // TODO: dequeue work from work_queue

        // TODO: Construct the coinbase transaction and compute the merkle root
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

        auth_to_stratum(sock, "johnny9.esp");

        char * extranonce_str;
        int extranonce_2_len;

        subscribe_to_stratum(sock, &extranonce_str, &extranonce_2_len);

        while (1)
        {
            char *line = receive_jsonrpc_line(sock);
            // Error occurred during receiving
            ESP_LOGI(TAG, "Json: %s", line);
            stratum_message parsed_message = parse_stratum_notify_message(line);
            if (parsed_message.method == STRATUM_UNKNOWN) {
                ESP_LOGI(TAG, "UNKNOWN MESSAGE");
            } else {
                ESP_LOGI(TAG, "method: %s", parsed_message.method_str);
                if (parsed_message.method == MINING_NOTIFY) {
                    queue_enqueue(&g_queue, parsed_message.notify_work);
                }
            }
            free(line);
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
