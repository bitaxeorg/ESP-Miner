#ifndef YYJSON_UTILS_H
#define YYJSON_UTILS_H

#include <stdbool.h>
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

/**
 * Recursively compares two JSON objects and returns a new object containing only the differences.
 * If old_obj is NULL, returns a deep copy of new_obj.
 * Nested objects are compared recursively. Scalars and arrays are copied if not equal.
 * Returns NULL if no differences exist or on failure. The returned value is allocated in doc.
 */
yyjson_mut_val *yyjson_diff_objects(yyjson_mut_doc *doc, yyjson_mut_val *old_obj, yyjson_mut_val *new_obj);

#endif // YYJSON_UTILS_H
