#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "esp_log.h"
#include "soc/uart_struct.h"

#include "pretty.h"
#include "bm1397.h"

#define ECHO_TEST_TXD  (17)
#define ECHO_TEST_RXD  (18)
#define BUF_SIZE (1024)

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
    //In this example we don't even use a buffer for sending data.
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
}

int send_serial(uint8_t *data, int len) {
    printf("->");
    prettyHex((unsigned char*)data, len);
    printf("\n");

    return uart_write_bytes(UART_NUM_1, (const char *) data, len);
}
 

void SerialTask(void *arg) {
    unsigned char data[BUF_SIZE];

    init_serial();

    init_BM1397();

    //reset the bm1397
    reset_BM1397();

    while(1) {
        //Read data from UART
        int len = uart_read_bytes(UART_NUM_1, data, BUF_SIZE, 20 / portTICK_RATE_MS);

        if (len > 0) {
            data[len] = 0;
            printf("Read %d bytes: ", len);
            prettyHex(data, len);
            printf("\n");
        }
    }
}