#ifndef BZM_ESP_SERIAL_H_
#define BZM_ESP_SERIAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "bzm/serial_buffers.h"
#include "bzm/protocol.h"

#define BZM_UART_FREQ 115200

#define BZM_SERIAL_HARDWARE_FIFO_BYTES 128U
#define BZM_SERIAL_RX_FIFO_FULL_THRESHOLD 32U
#define BZM_SERIAL_WIRE_BYTES_PER_SECOND (BZM_BAUD_RATE / 10U)
#define BZM_SERIAL_MAX_ISR_LATENCY_US 150U

static inline bool BZM_SERIAL_fifo_reserve_covers(uint32_t fifo_bytes,
                                              uint32_t full_threshold,
                                              uint32_t bytes_per_second,
                                              uint32_t latency_us)
{
    if (full_threshold >= fifo_bytes || bytes_per_second == 0) {
        return false;
    }
    return (uint64_t) (fifo_bytes - full_threshold) * 1000000U >=
           (uint64_t) bytes_per_second * latency_us;
}

esp_err_t BZM_SERIAL_init(void);
int16_t BZM_SERIAL_rx(uint8_t *, uint16_t, uint16_t);
esp_err_t BZM_SERIAL_set_baud(int baud);
esp_err_t BZM_SERIAL_wait_tx_done(uint32_t timeout_ms);

#endif /* BZM_ESP_SERIAL_H_ */
