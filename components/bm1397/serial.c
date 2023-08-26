#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "esp_log.h"
#include "soc/uart_struct.h"

#include "bm1397.h"
#include "serial.h"
#include "utils.h"

#define ECHO_TEST_TXD (17)
#define ECHO_TEST_RXD (18)
#define BUF_SIZE (1024)

static const char *TAG = "serial";

void SERIAL_init(void)
{
    ESP_LOGI(TAG, "Initializing serial");
    // Configure UART1 parameters
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure UART1 parameters
    uart_param_config(UART_NUM_1, &uart_config);
    // Set UART1 pins(TX: IO17, RX: I018)
    uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Install UART driver (we don't need an event queue here)
    // tx buffer 0 so the tx time doesn't overlap with the job wait time
    //  by returning before the job is written
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);
}

void SERIAL_set_baud(int baud)
{
    ESP_LOGI(TAG, "Changing UART baud to %i", baud);
    uart_set_baudrate(UART_NUM_1, baud);
}

int SERIAL_send(uint8_t *data, int len, bool debug)
{
    if (debug)
    {
        printf("->");
        prettyHex((unsigned char *)data, len);
        printf("\n");
    }

    return uart_write_bytes(UART_NUM_1, (const char *)data, len);
}

/// @brief waits for a serial response from the device
/// @param buf buffer to read data into
/// @param buf number of ms to wait before timing out
/// @return number of bytes read, or -1 on error
int16_t SERIAL_rx(uint8_t *buf, uint16_t size, uint16_t timeout_ms)
{
    int16_t bytes_read = uart_read_bytes(UART_NUM_1, buf, size, timeout_ms / portTICK_PERIOD_MS);
    // if (bytes_read > 0) {
    //     printf("rx: ");
    //     prettyHex((unsigned char*) buf, bytes_read);
    //     printf("\n");
    // }
    return bytes_read;
}

void SERIAL_debug_rx(void)
{
    int ret;
    uint8_t buf[CHUNK_SIZE];

    ret = SERIAL_rx(buf, 100, 20);
    if (ret < 0)
    {
        fprintf(stderr, "unable to read data\n");
        return;
    }

    memset(buf, 0, 1024);
}

void SERIAL_clear_buffer(void)
{
    uart_flush(UART_NUM_1);
}
