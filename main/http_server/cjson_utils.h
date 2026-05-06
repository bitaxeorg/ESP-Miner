#ifndef CJSON_UTILS_H_
#define CJSON_UTILS_H_

#include "cJSON.h"

cJSON* cJSON_AddFloatToObject(cJSON * const object, const char * const name, const float number);

cJSON* cJSON_CreateFloat(float number);

/**
 * @brief Recursively compares two JSON objects and returns a new object containing only the differences.
 * 
 * If a field is present in new_json but not in old_json, or if the value is different,
 * it is added to the resulting diff object.
 * 
 * For nested objects, it recurses to find granular differences.
 * For arrays, it performs a full replacement if the arrays are not identical.
 * 
 * @param old_json The baseline JSON object (can be NULL)
 * @param new_json The target JSON object
 * @return cJSON* A new JSON object containing only the differences, or NULL if no differences exist.
 *                The caller is responsible for deleting the returned object.
 */
cJSON* cJSON_GetDiff(const cJSON *old_json, const cJSON *new_json);

#endif /* CJSON_UTILS_H_ */
