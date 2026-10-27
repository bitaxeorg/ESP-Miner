#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "esp_log.h"

#include "bzm/serial.h"
#include "serial.h"
#include "serial_internal.h"

#define ECHO_TEST_TXD (17)
#define ECHO_TEST_RXD (18)

static const char * TAG = "serial";

esp_err_t BZM_SERIAL_init(void)
{
    ESP_LOGI(TAG, "Initializing serial");
    // Configure UART1 parameters
    uart_config_t uart_config = {
        .baud_rate = BZM_UART_FREQ,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure UART1 parameters
    esp_err_t err = uart_param_config(UART_NUM_1, &uart_config);
    if (err != ESP_OK) {
        return err;
    }
    // Set UART1 pins(TX: IO17, RX: I018)
    err = uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    // Retain RX headroom for scheduler and interrupt delays between polls.
    err = uart_driver_install(
        UART_NUM_1, BZM_SERIAL_RX_BUFFER_BYTES, BZM_SERIAL_TX_BUFFER_BYTES, 0, NULL, 0
    );
    if (err != ESP_OK) {
        return err;
    }

    /* The ESP32-S3 UART FIFO is only 128 bytes. Keep the early threshold even
     * at the signal-qualified 2 Mbaud bridge rate, so the remaining 96 bytes
     * absorb bounded interrupt latency before the 32 KiB software ring takes
     * ownership. */
    err = uart_set_rx_full_threshold(UART_NUM_1,
                                     BZM_SERIAL_RX_FIFO_FULL_THRESHOLD);
    if (err != ESP_OK) {
        (void) uart_driver_delete(UART_NUM_1);
        return err;
    }
    return ESP_OK;
}

esp_err_t BZM_SERIAL_set_baud(int baud)
{
    ESP_LOGI(TAG, "Changing UART baud to %i", baud);

    // Make sure that we are done writing before setting a new baudrate.
    esp_err_t err = uart_wait_tx_done(UART_NUM_1, 1000 / portTICK_PERIOD_MS);
    if (err != ESP_OK) {
        return err;
    }

    return uart_set_baudrate(UART_NUM_1, baud);
}

esp_err_t BZM_SERIAL_wait_tx_done(uint32_t timeout_ms)
{
    if (!SERIAL_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms != 0 && timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    return uart_wait_tx_done(UART_NUM_1, timeout_ticks);
}

static TickType_t timeout_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == 0) return 0;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return ticks == 0 ? 1 : ticks;
}

int16_t BZM_SERIAL_rx(uint8_t * buf, uint16_t size, uint16_t timeout_ms)
{
    if (buf == NULL || size == 0 || !SERIAL_is_initialized()) return -1;
    return SERIAL_rx_ticks(buf, size, timeout_ticks(timeout_ms));
}
