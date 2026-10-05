#include "yyjson_psram.h"
#include <esp_heap_caps.h>

static void *psram_malloc(void *ctx, size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void *psram_realloc(void *ctx, void *ptr, size_t old_size, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void psram_free(void *ctx, void *ptr)
{
    heap_caps_free(ptr);
}

static const yyjson_alc s_yyjson_psram_alc = {
    .malloc = psram_malloc,
    .realloc = psram_realloc,
    .free = psram_free,
    .ctx = NULL,
};

const yyjson_alc *yyjson_psram_alc(void)
{
    return &s_yyjson_psram_alc;
}

void yyjson_alc_free(const yyjson_alc *alc, void *ptr)
{
    if (!ptr) {
        return;
    }
    if (alc && alc->free) {
        alc->free(alc->ctx, ptr);
    } else {
        free(ptr);
    }
}
