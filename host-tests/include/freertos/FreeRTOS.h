#ifndef ESP_MINER_HOST_FREERTOS_H
#define ESP_MINER_HOST_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))

#endif
