/**
 * @file bap_uart.c
 * @brief BAP UART communication layer
 * 
 * Handles UART initialization, message sending/receiving, and communication tasks.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "device_config.h"
#include "bap_uart.h"
#include "bap_protocol.h"
#include "bap.h"

#define BAP_UART_NUM UART_NUM_2
#define BAP_BUF_SIZE 1024
#define UART_SEND_QUEUE_SIZE 10
#define UART_SEND_QUEUE_ITEM_SIZE sizeof(bap_message_t)
#define UART_SEND_TIMEOUT_MS 1000
#define UART_BUFFER_THRESHOLD (BAP_BUF_SIZE / 2)

#define GPIO_BAP_RX CONFIG_GPIO_BAP_RX
#define GPIO_BAP_TX CONFIG_GPIO_BAP_TX

static const char *TAG = "BAP_UART";

static TaskHandle_t uart_send_task_handle = NULL;
static TaskHandle_t uart_receive_task_handle = NULL;

static void uart_receive_task(void *pvParameters);
static void uart_send_task(void *pvParameters);

extern void BAP_parse_message(const char *message);

void BAP_send_message(bap_command_t cmd, const char *parameter, const char *value) {
    char message[BAP_MAX_MESSAGE_LEN];
    char sentence_body[BAP_MAX_MESSAGE_LEN];
    int len;

    if (value && strlen(value) > 0) {
        snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s,%s",
                 BAP_command_to_string(cmd), parameter, value);
    } else {
        snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s",
                 BAP_command_to_string(cmd), parameter);
    }

    uint8_t checksum = BAP_calculate_checksum(sentence_body);

    len = snprintf(message, sizeof(message), "$%s*%02X\r\n", sentence_body, checksum);

    if (bap_uart_send_mutex != NULL && xSemaphoreTake(bap_uart_send_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_write_bytes(BAP_UART_NUM, message, len);
        xSemaphoreGive(bap_uart_send_mutex);
        
        ESP_LOGI(TAG, "Sent: %s", message);
    } else {
        ESP_LOGW(TAG, "Failed to take UART mutex for immediate send, message dropped");
    }
}

void BAP_send_message_with_queue(bap_command_t cmd, const char *parameter, const char *value) {
    char message[BAP_MAX_MESSAGE_LEN];
    char sentence_body[BAP_MAX_MESSAGE_LEN];
    int len;
    bap_message_t msg;

    if (value && strlen(value) > 0) {
        snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s,%s",
                 BAP_command_to_string(cmd), parameter, value);
    } else {
        snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s",
                 BAP_command_to_string(cmd), parameter);
    }

    uint8_t checksum = BAP_calculate_checksum(sentence_body);

    len = snprintf(message, sizeof(message), "$%s*%02X\r\n", sentence_body, checksum);

    msg.message = malloc(len + 1);
    if (msg.message == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        return;
    }
    
    strncpy(msg.message, message, len);
    msg.message[len] = '\0';
    msg.length = len;

    if (xQueueSend(bap_uart_send_queue, &msg, UART_SEND_TIMEOUT_MS / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue message, dropping");
        free(msg.message);
    }
}

void BAP_send_init_message(GlobalState *state) {
    const char *init_message = "BAP UART Interface Initialized\r\n";
    esp_err_t ret = uart_write_bytes(BAP_UART_NUM, init_message, strlen(init_message));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send init message: %d", ret);
    } else {
        //ESP_LOGI(TAG, "Init message sent: %s", init_message);
    }
}

void BAP_send_ap_message(GlobalState *state) {
    BAP_send_message(BAP_CMD_CMD, "mode", "ap_mode");
}


static void uart_receive_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(BAP_BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART receive buffer");
        vTaskDelete(NULL);
        return;
    }

    char message[BAP_MAX_MESSAGE_LEN + 1];
    size_t message_len = 0;
    bool in_message = false;

    while (1) {
        int len = uart_read_bytes(BAP_UART_NUM, data, BAP_BUF_SIZE, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            ESP_LOGD(TAG, "Received %d bytes from UART", len);
            
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                
                if (c == '$') {
                    if (!in_message) {
                        ESP_LOGI(TAG, "Start of message detected");
                    }
                    in_message = true;
                    message_len = 0;
                    message[message_len++] = c;
                }
                else if ((c == '\n' || c == '\r') && in_message) {
                    if (message_len > 1 && message_len < BAP_MAX_MESSAGE_LEN) {
                        message[message_len++] = c;
                        message[message_len] = '\0';
                        
                        //ESP_LOGI(TAG, "Received complete message: %s", message);
                        BAP_parse_message(message);
                        
                        if (c == '\r') {
                            ESP_LOGD(TAG, "Got CR, waiting for possible LF");
                        } else {
                            in_message = false;
                        }
                    } else if (message_len >= BAP_MAX_MESSAGE_LEN) {
                        ESP_LOGE(TAG, "Message too long, discarding");
                        in_message = false;
                    }
                }
                else if (in_message && message_len < BAP_MAX_MESSAGE_LEN) {
                    message[message_len++] = c;
                    ESP_LOGD(TAG, "Added to message: %c, len now %d", c, message_len);
                }
            }
        }
    }

    free(data);
    vTaskDelete(NULL);
}

static void uart_send_task(void *pvParameters) {
    bap_message_t msg;
    while (1) {
        if (xQueueReceive(bap_uart_send_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            if (bap_uart_send_mutex != NULL && xSemaphoreTake(bap_uart_send_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                size_t available;
                esp_err_t ret = uart_get_buffered_data_len(BAP_UART_NUM, &available);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to get UART buffer status");
                    xSemaphoreGive(bap_uart_send_mutex);
                } else if (available > UART_BUFFER_THRESHOLD) {
                    ESP_LOGW(TAG, "UART buffer threshold exceeded (%zu bytes), dropping message", available);
                    xSemaphoreGive(bap_uart_send_mutex);
                } else {
                    int bytes_sent = uart_write_bytes(BAP_UART_NUM, msg.message, msg.length);
                    if (bytes_sent == msg.length) {
                    } else {
                        ESP_LOGW(TAG, "UART send failed or partial: %d of %d bytes", bytes_sent, msg.length);
                    }
                    xSemaphoreGive(bap_uart_send_mutex);
                }
            } else {
                ESP_LOGW(TAG, "Failed to take UART send mutex, dropping message");
            }

            if (msg.message) {
                free(msg.message);
                msg.message = NULL;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t BAP_start_uart_receive_task(void) {
    xTaskCreateWithCaps(
        uart_receive_task,
        "uart_receive_ta",
        8192,
        NULL,
        5,
        &uart_receive_task_handle,
        MALLOC_CAP_SPIRAM
    );

    //ESP_LOGI(TAG, "UART receive task started");
    return ESP_OK;
}

esp_err_t BAP_uart_init(void) {
    ESP_LOGI(TAG, "Initializing BAP UART interface");
    
    if (GPIO_BAP_TX > 47 || GPIO_BAP_RX > 47) {
        ESP_LOGE(TAG, "Invalid GPIO pins: TX=%d, RX=%d", GPIO_BAP_TX, GPIO_BAP_RX);
        return ESP_ERR_INVALID_ARG;
    }
    
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    
    esp_err_t ret = uart_param_config(BAP_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %d", ret);
        return ret;
    }
    
    ret = uart_set_pin(BAP_UART_NUM, GPIO_BAP_TX, GPIO_BAP_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %d", ret);
        return ret;
    }
    
    ret = uart_driver_install(BAP_UART_NUM, BAP_BUF_SIZE, BAP_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %d", ret);
        return ret;
    }
    
    ESP_LOGI(TAG, "BAP UART interface initialized successfully");
    
    BaseType_t task_result = xTaskCreateWithCaps(
        uart_send_task,
        "uart_send_task",
        8192,
        NULL,
        5,
        &uart_send_task_handle,
        MALLOC_CAP_SPIRAM
    );
    
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create uart_send_task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "UART send task created successfully");
    return ESP_OK;
}