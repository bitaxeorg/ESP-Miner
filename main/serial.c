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
#include "serial.h"

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
    //In this example we don't even use a buffer for sending data.
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
}

int send_serial(uint8_t *data, int len, bool debug) {
    if (debug) {
        printf("->");
        prettyHex((unsigned char*)data, len);
    }

    return uart_write_bytes(UART_NUM_1, (const char *) data, len);
}

/// @brief waits for a serial response from the device
/// @param buf buffer to read data into
/// @return number of bytes read, or -1 on error
int16_t serial_rx(uint8_t * buf) {
    int16_t bytes_read = uart_read_bytes(UART_NUM_1, buf, BUF_SIZE, 20 / portTICK_RATE_MS);
    if (bytes_read > 0) {
        printf("bm rx\n");
        prettyHex((unsigned char*) buf, bytes_read);
    }
    return bytes_read;
}

void debug_serial_rx(void) {
    int ret;
    uint8_t buf[CHUNK_SIZE];

    ret = serial_rx(buf);
    if (ret < 0) {
        fprintf(stderr, "unable to read data\n");
        return;
    }

    if (ret > 0) {
        split_response(buf, ret);
    }

    memset(buf, 0, 1024);

}

void SerialTask(void *arg) {

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

    //setup a nice test job
    //this should find nonce 258a8b34 @ 50
    // uint8_t work1[146] = {0x50, 0x04, 0x00, 0x00, 0x00, 0x00, 0xB2, 0xE0, 0x05, 0x17, 0x24, 0x27, 0x36, 0x64, 0xF5, 0x63, 0x54, 0xDA, 0x33, 0xE2, 0xDE, 0x8F, 0xFC, 0xDD, 0x48, 0x96, 0xE1, 0x36, 0xD7, 0x03, 0x5C, 0xBB, 0x5F, 0xA3, 0xFD, 0x5F, 0x68, 0x39, 0xAA, 0xA4, 0xBE, 0x10, 0x9C, 0x7E, 0x00, 0x78, 0x4E, 0x69, 0x34, 0xAC, 0x84, 0x05, 0x65, 0xAE, 0x32, 0x58, 0x09, 0xBB, 0xEA, 0x44, 0x6D, 0x61, 0x57, 0xF2, 0x61, 0xBE, 0x58, 0x33, 0xFA, 0xA8, 0x1D, 0x9A, 0x16, 0xBF, 0xE0, 0x82, 0x64, 0x37, 0x91, 0x15, 0xB6, 0x32, 0x93, 0xC4, 0x83, 0x42, 0xB2, 0xE6, 0x63, 0x96, 0xE0, 0x25, 0x02, 0x9E, 0x01, 0x76, 0xD9, 0x24, 0x0F, 0xD3, 0x57, 0x27, 0x38, 0xE2, 0x65, 0xDD, 0xCD, 0xBD, 0x01, 0xE0, 0x61, 0xFB, 0x57, 0x5D, 0xD6, 0xAB, 0xAE, 0xFD, 0x6B, 0x5F, 0x77, 0x74, 0x5C, 0x64, 0x2C, 0xF3, 0x34, 0x2F, 0x82, 0xB3, 0xCC, 0xC1, 0x2D, 0x84, 0xDD, 0xCB, 0x10, 0xDE, 0x5E, 0xE0, 0xCD, 0x9C, 0x5B, 0x65, 0x92, 0xBB};
    // struct job_packet test_job;
    // memcpy((uint8_t *)&test_job, work1, 146);

    // send_work(&test_job);

    while (1) {
        debug_serial_rx();
    }
}