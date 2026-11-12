#ifndef BZM_PLATFORM_SERIAL_H_
#define BZM_PLATFORM_SERIAL_H_

#include "bzm/platform.h"

#include <stdbool.h>
#include <stdint.h>

#include "bzm/transport.h"

extern const bzm_serial_ops_t bzm_platform_serial_ops;

int bzm_port_serial_send(uint8_t *, int, bool);
bzm_status_t bzm_port_serial_init(void);
bzm_status_t bzm_port_serial_ensure_initialized(int baud);
bzm_status_t bzm_port_serial_prepare_session(int baud);
int16_t bzm_port_serial_rx(uint8_t *, uint16_t, uint16_t);
void bzm_port_serial_clear_buffer(void);
bzm_status_t bzm_port_serial_set_baud(int baud);
bzm_status_t bzm_port_serial_wait_tx_done(uint32_t timeout_ms);
bool bzm_port_serial_is_initialized(void);

#endif /* BZM_PLATFORM_SERIAL_H_ */
