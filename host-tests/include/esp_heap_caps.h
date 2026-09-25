#ifndef ESP_MINER_HOST_ESP_HEAP_CAPS_H
#define ESP_MINER_HOST_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0

static inline void *heap_caps_malloc(size_t size, unsigned capabilities)
{
    (void)capabilities;
    return malloc(size);
}

static inline void *heap_caps_calloc(size_t count, size_t size, unsigned capabilities)
{
    (void)capabilities;
    return calloc(count, size);
}

#endif
