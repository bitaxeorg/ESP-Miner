#ifndef BZM_BZM_SERIAL_H_
#define BZM_BZM_SERIAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define BZM_UART_FREQ 115200

/* A complete BZM work dispatch holds the reactor lock while all 944 engine
 * writes are issued.  The UART driver must retain the continuously arriving
 * TDM telemetry and result stream until the polling task can run again. */
#define BZM_SERIAL_RX_BUFFER_BYTES (32U * 1024U)
#define BZM_SERIAL_TX_BUFFER_BYTES (2U * 1024U)
#define BZM_SERIAL_RX_DESIGN_RATE_BYTES_PER_SECOND 16000U
#define BZM_SERIAL_RX_MAX_DISPATCH_BLACKOUT_MS 1250U
#define BZM_SERIAL_HARDWARE_FIFO_BYTES 128U
#define BZM_SERIAL_RX_FIFO_FULL_THRESHOLD 32U
#define BZM_SERIAL_WIRE_BYTES_PER_SECOND 200000U
#define BZM_SERIAL_MAX_ISR_LATENCY_US 150U

static inline bool BZM_SERIAL_buffer_capacity_covers(uint32_t buffer_bytes, uint32_t bytes_per_second,
                                                 uint32_t blackout_ms)
{
    return (uint64_t) buffer_bytes * 1000U >= (uint64_t) bytes_per_second * blackout_ms;
}

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

int BZM_SERIAL_send(uint8_t *, int, bool);
esp_err_t BZM_SERIAL_init(void);
esp_err_t BZM_SERIAL_ensure_initialized(int baud);
esp_err_t BZM_SERIAL_prepare_session(int baud);
int16_t BZM_SERIAL_rx(uint8_t *, uint16_t, uint16_t);
void BZM_SERIAL_clear_buffer(void);
esp_err_t BZM_SERIAL_set_baud(int baud);
esp_err_t BZM_SERIAL_wait_tx_done(uint32_t timeout_ms);
bool BZM_SERIAL_is_initialized(void);

#endif /* BZM_BZM_SERIAL_H_ */
