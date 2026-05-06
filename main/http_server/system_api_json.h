#ifndef SYSTEM_API_JSON_H_
#define SYSTEM_API_JSON_H_

#include "cJSON.h"
#include "global_state.h"

/**
 * @brief Generates a full system information JSON object.
 * 
 * This is the single source of truth for both the REST API and the WebSocket initial handshake.
 * 
 * @param g Pointer to the GlobalState structure.
 * @return cJSON* The root JSON object. Caller is responsible for cJSON_Delete().
 */
cJSON* system_api_get_full_json(GlobalState *g);

/**
 * @brief Custom helper to create a JSON number from a float with fixed decimal precision.
 */
cJSON* cJSON_CreateFloat(float number);

#endif /* SYSTEM_API_JSON_H_ */
