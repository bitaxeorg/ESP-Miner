#ifndef SYSTEM_API_JSON_H_
#define SYSTEM_API_JSON_H_

#include "yyjson.h"

typedef struct GlobalState GlobalState;

/**
 * @brief Generates a full system information JSON document.
 * 
 * This is the single source of truth for both the REST API and the WebSocket updates.
 * 
 * @param GLOBAL_STATE Pointer to the GlobalState structure.
 * @param alc Custom memory allocator (e.g. yyjson_psram_alc()) or NULL for default.
 * @return yyjson_mut_doc* The JSON document. Caller is responsible for yyjson_mut_doc_free().
 */
yyjson_mut_doc* system_api_get_full_doc(GlobalState * GLOBAL_STATE, const yyjson_alc *alc);

#endif /* SYSTEM_API_JSON_H_ */
