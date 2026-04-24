#include <math.h>
#include <stdbool.h>
#include "cjson_utils.h"

static const double FACTOR = 10000000.0;

cJSON* cJSON_AddFloatToObject(cJSON * const object, const char * const name, const float number) {
    double d_value = round((double)number * FACTOR) / FACTOR;
    return cJSON_AddNumberToObject(object, name, d_value);
}

cJSON* cJSON_CreateFloat(float number) {
    double d_value = round((double)number * FACTOR) / FACTOR;
    return cJSON_CreateNumber(d_value);
}

cJSON* cJSON_GetDiff(const cJSON *old_json, const cJSON *new_json) {
    if (new_json == NULL) return NULL;
    
    // If there is no previous state, the diff is the entire new object
    if (old_json == NULL) {
        return cJSON_Duplicate(new_json, true);
    }

    cJSON *diff = cJSON_CreateObject();
    if (!diff) return NULL;

    cJSON *new_item = NULL;
    cJSON_ArrayForEach(new_item, new_json) {
        cJSON *old_item = cJSON_GetObjectItemCaseSensitive(old_json, new_item->string);
        
        // If the item doesn't exist in the old version, or is fundamentally different
        if (old_item == NULL || !cJSON_Compare(old_item, new_item, true)) {
            
            // If it's a nested object and we HAVE an old version to compare against, recurse for granular diff
            if (cJSON_IsObject(new_item) && old_item && cJSON_IsObject(old_item)) {
                cJSON *sub_diff = cJSON_GetDiff(old_item, new_item);
                if (sub_diff) {
                    cJSON_AddItemToObject(diff, new_item->string, sub_diff);
                }
            } else {
                // Otherwise (scalars, arrays, or completely new objects), just duplicate the new value
                cJSON_AddItemToObject(diff, new_item->string, cJSON_Duplicate(new_item, true));
            }
        }
    }

    // If no differences were found, clean up the empty object and return NULL
    if (diff->child == NULL) {
        cJSON_Delete(diff);
        return NULL;
    }

    return diff;
}
