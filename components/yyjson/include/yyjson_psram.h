#ifndef YYJSON_PSRAM_H
#define YYJSON_PSRAM_H

#include "yyjson.h"

/**
 * Returns a pointer to a yyjson_alc configured to allocate from external PSRAM (SPIRAM).
 */
const yyjson_alc *yyjson_psram_alc(void);

/**
 * Convenience helper to free memory allocated by a yyjson_alc (e.g. from yyjson_mut_write_opts).
 * Safely calls alc->free(alc->ctx, ptr) if alc and alc->free are set, otherwise calls standard free(ptr).
 */
void yyjson_alc_free(const yyjson_alc *alc, void *ptr);

#endif // YYJSON_PSRAM_H
