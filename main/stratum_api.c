#include "stratum_api.h"
#include "cJSON.h"
#include <string.h>

stratum_message parse_stratum_message(const char * stratum_json)
{
    stratum_message output;
    output.method = STRATUM_UNKNOWN;
    cJSON *json = cJSON_Parse(stratum_json);
    if (json == NULL) {
        return output;
    }

    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *method = cJSON_GetObjectItem(json, "method");

    if (id != NULL && cJSON_IsNumber(id)) {
        output.id = id->valueint;
    }

    if (method != NULL && cJSON_IsString(method)) {
        output.method_str = strdup(method->valuestring);
        if (strcmp("mining.notify", method->valuestring) == 0) {
            output.method = MINING_NOTIFY;
        } else if (strcmp("mining.set_difficulty", method->valuestring) == 0) {
            output.method = MINING_SET_DIFFICULTY;
        } else {
            output.method = STRATUM_UNKNOWN;
        }
    }

    cJSON_Delete(json);
    return output;
}