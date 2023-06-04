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

#define ECHO_TEST_TXD   (17)
#define ECHO_TEST_RXD   (18)
#define BUF_SIZE        (1024)

static const char *TAG = "serial";

void init_serial(void) {
    ESP_LOGI(TAG, "Initializing serial");
    //Configure UART1 parameters
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    //Configure UART1 parameters
    uart_param_config(UART_NUM_1, &uart_config);
    //Set UART1 pins(TX: IO17, RX: I018)
    uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    //Install UART driver (we don't need an event queue here)
    // Added buffers so uart_write_bytes returns immediately 
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);
}

void set_max_baud(void){
    ESP_LOGI("SERIAL", "SETTING CHIP MAX BAUD");
    set_bm1397_max_baud();
    ESP_LOGI("SERIAL", "SETTING UART MAX BAUD");
    uart_set_baudrate(UART_NUM_1, 3125000);
}

int send_serial(uint8_t *data, int len, bool debug) {
    if (debug) {
        printf("->");
        prettyHex((unsigned char*)data, len);
        printf("\n");
    }

    return uart_write_bytes(UART_NUM_1, (const char *) data, len);
}

/// @brief waits for a serial response from the device
/// @param buf buffer to read data into
/// @param buf number of ms to wait before timing out
/// @return number of bytes read, or -1 on error
int16_t serial_rx(uint8_t * buf, uint16_t size, uint16_t timeout_ms) {
    int16_t bytes_read = uart_read_bytes(UART_NUM_1, buf, size, timeout_ms / portTICK_PERIOD_MS);
    // if (bytes_read > 0) {
    //     printf("rx: ");
    //     prettyHex((unsigned char*) buf, bytes_read);
    //     printf("\n");
    // }
    return bytes_read;
}

void debug_serial_rx(void) {
    int ret;
    uint8_t buf[CHUNK_SIZE];

    ret = serial_rx(buf, 100, 20);
    if (ret < 0) {
        fprintf(stderr, "unable to read data\n");
        return;
    }

    memset(buf, 0, 1024);

}

void clear_serial_buffer(void) {
    uart_flush(UART_NUM_1);
}
