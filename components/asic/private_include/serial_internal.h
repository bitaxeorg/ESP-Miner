#ifndef SERIAL_INTERNAL_H_
#define SERIAL_INTERNAL_H_

#include <stdint.h>
#include "freertos/FreeRTOS.h"

int16_t SERIAL_rx_ticks(uint8_t *buf, uint16_t size, TickType_t timeout_ticks);

#endif
