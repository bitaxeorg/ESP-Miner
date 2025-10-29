/******************************************************************************
 *  *
 * References:
 *  1. Stratum Protocol - [link](https://reference.cash/mining/stratum-protocol)
 *****************************************************************************/

#include "stratum_api.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include "utils.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define BUFFER_SIZE 1024
#define MAX_EXTRANONCE_2_LEN 32
static const char * TAG = "stratum_api";

static char * json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;
static int last_parsed_request_id = -1;

static RequestTiming request_timings[MAX_REQUEST_IDS];
static bool initialized = false;

static void init_request_timings() {
    if (!initialized) {
        for (int i = 0; i < MAX_REQUEST_IDS; i++) {
            request_timings[i].timestamp_us = 0;
            request_timings[i].tracking = false;
        }
        initialized = true;
    }
}

static RequestTiming* get_request_timing(int request_id) {
    if (request_id < 0) return NULL;
    int index = request_id % MAX_REQUEST_IDS;
    return &request_timings[index];
}

void STRATUM_V1_stamp_tx(int request_id)
{
    init_request_timings();
    if (request_id >= 1) {
        RequestTiming *timing = get_request_timing(request_id);
        if (timing) {
            timing->timestamp_us = esp_timer_get_time();
            timing->tracking = true;
        }
    }
}

double STRATUM_V1_get_response_time_ms(int request_id)
{
    init_request_timings();
    if (request_id < 0) return -1.0;
    
    RequestTiming *timing = get_request_timing(request_id);
    if (!timing || !timing->tracking) {
        return -1.0;
    }
    
    double response_time = (esp_timer_get_time() - timing->timestamp_us) / 1000.0;
    timing->tracking = false;
    return response_time;
}

static void debug_stratum_tx(const char *);
int _parse_stratum_subscribe_result_message(const char * result_json_str, char ** extranonce, int * extranonce2_len);

void STRATUM_V1_initialize_buffer()
{
    json_rpc_buffer = malloc(BUFFER_SIZE);
    json_rpc_buffer_size = BUFFER_SIZE;
    if (json_rpc_buffer == NULL) {
        printf("Error: Failed to allocate memory for buffer\n");
        exit(1);
    }
    memset(json_rpc_buffer, 0, BUFFER_SIZE);
}

void cleanup_stratum_buffer()
{
    free(json_rpc_buffer);
}

static void realloc_json_buffer(size_t len)
{
    size_t old, new;

    old = strlen(json_rpc_buffer);
    new = old + len + 1;

    if (new < json_rpc_buffer_size) {
        return;
    }

    new = new + (BUFFER_SIZE - (new % BUFFER_SIZE));
    void * new_sockbuf = realloc(json_rpc_buffer, new);

    if (new_sockbuf == NULL) {
        fprintf(stderr, "Error: realloc failed in recalloc_sock()\n");
        ESP_LOGI(TAG, "Restarting System because of ERROR: realloc failed in recalloc_sock");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    json_rpc_buffer = new_sockbuf;
    memset(json_rpc_buffer + old, 0, new - old);
    json_rpc_buffer_size = new;
}

char * STRATUM_V1_receive_jsonrpc_line(int sockfd)
{
    if (json_rpc_buffer == NULL) {
        STRATUM_V1_initialize_buffer();
    }
    char *line, *tok = NULL;
    char recv_buffer[BUFFER_SIZE];
    int nbytes;
    size_t buflen = 0;

    if (!strstr(json_rpc_buffer, "\n")) {
        do {
            memset(recv_buffer, 0, BUFFER_SIZE);
            nbytes = recv(sockfd, recv_buffer, BUFFER_SIZE - 1, 0);
            if (nbytes == -1) {
                ESP_LOGI(TAG, "Error: recv (errno %d: %s)", errno, strerror(errno));
                if (json_rpc_buffer) {
                    free(json_rpc_buffer);
                    json_rpc_buffer=0;
                }
                return 0;
            }

            realloc_json_buffer(nbytes);
            strncat(json_rpc_buffer, recv_buffer, nbytes);
        } while (!strstr(json_rpc_buffer, "\n"));
    }
    buflen = strlen(json_rpc_buffer);
    tok = strtok(json_rpc_buffer, "\n");
    line = strdup(tok);
    int len = strlen(line);
    if (buflen > len + 1)
        memmove(json_rpc_buffer, json_rpc_buffer + len + 1, buflen - len + 1);
    else
        strcpy(json_rpc_buffer, "");
    return line;
}

void STRATUM_V1_parse(StratumApiV1Message * message, const char * stratum_json)
{
    ESP_LOGI(TAG, "rx: %s", stratum_json); // debug incoming stratum messages

    cJSON * json = cJSON_Parse(stratum_json);

    cJSON * id_json = cJSON_GetObjectItem(json, "id");
    int64_t parsed_id = -1;
    if (id_json != NULL && cJSON_IsNumber(id_json)) {
        parsed_id = id_json->valueint;
    }
    last_parsed_request_id = parsed_id;
    message->message_id = parsed_id;

    cJSON * method_json = cJSON_GetObjectItem(json, "method");
    stratum_method result = STRATUM_UNKNOWN;

    //if there is a method, then use that to decide what to do
    if (method_json != NULL && cJSON_IsString(method_json)) {
        if (strcmp("mining.notify", method_json->valuestring) == 0) {
            result = MINING_NOTIFY;
        } else if (strcmp("mining.set_difficulty", method_json->valuestring) == 0) {
            result = MINING_SET_DIFFICULTY;
        } else if (strcmp("mining.set_version_mask", method_json->valuestring) == 0) {
            result = MINING_SET_VERSION_MASK;
        } else if (strcmp("mining.set_extranonce", method_json->valuestring) == 0) {
            result = MINING_SET_EXTRANONCE;
        } else if (strcmp("client.reconnect", method_json->valuestring) == 0) {
            result = CLIENT_RECONNECT;
        } else {
            ESP_LOGI(TAG, "unhandled method in stratum message: %s", stratum_json);
        }

    //if there is no method, then it is a result
    } else {
        // parse results
        cJSON * result_json = cJSON_GetObjectItem(json, "result");
        cJSON * error_json = cJSON_GetObjectItem(json, "error");
        cJSON * reject_reason_json = cJSON_GetObjectItem(json, "reject-reason");

        // if the result is null, then it's a fail
        if (result_json == NULL) {
            message->response_success = false;
            message->error_str = strdup("unknown");
            
        // if it's an error, then it's a fail
        } else if (error_json != NULL && !cJSON_IsNull(error_json)) {
            message->response_success = false;
            message->error_str = strdup("unknown");
            if (parsed_id < 5) {
                result = STRATUM_RESULT_SETUP;
            } else {
                result = STRATUM_RESULT;
            }
            if (cJSON_IsArray(error_json)) {
                int len = cJSON_GetArraySize(error_json);
                if (len >= 2) {
                    cJSON * error_msg = cJSON_GetArrayItem(error_json, 1);
                    if (cJSON_IsString(error_msg)) {
                        message->error_str = strdup(cJSON_GetStringValue(error_msg));
                    }
                }
            }

        // if the result is a boolean, then parse it
        } else if (cJSON_IsBool(result_json)) {
            if (parsed_id < 5) {
                result = STRATUM_RESULT_SETUP;
            } else {
                result = STRATUM_RESULT;
            }
            if (cJSON_IsTrue(result_json)) {
                message->response_success = true;
            } else {
                message->response_success = false;
                message->error_str = strdup("unknown");
                if (cJSON_IsString(reject_reason_json)) {
                    message->error_str = strdup(cJSON_GetStringValue(reject_reason_json));
                }                
            }
        
        //if the id is STRATUM_ID_SUBSCRIBE parse it
        } else if (parsed_id == STRATUM_ID_SUBSCRIBE) {
            result = STRATUM_RESULT_SUBSCRIBE;

            cJSON * extranonce2_len_json = cJSON_GetArrayItem(result_json, 2);
            if (extranonce2_len_json == NULL) {
                ESP_LOGE(TAG, "Unable to parse extranonce2_len: %s", result_json->valuestring);
                message->response_success = false;
                goto done;
            }
            int extranonce_2_len = extranonce2_len_json->valueint;
            if (extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                ESP_LOGW(TAG, "Extranonce_2_len %d exceeds maximum %d, clamping to maximum", 
                         extranonce_2_len, MAX_EXTRANONCE_2_LEN);
                extranonce_2_len = MAX_EXTRANONCE_2_LEN;
            }
            message->extranonce_2_len = extranonce_2_len;

            cJSON * extranonce_json = cJSON_GetArrayItem(result_json, 1);
            if (extranonce_json == NULL) {
                ESP_LOGE(TAG, "Unable parse extranonce: %s", result_json->valuestring);
                message->response_success = false;
                goto done;
            }
            message->extranonce_str = strdup(extranonce_json->valuestring);
            message->response_success = true;
        //if the id is STRATUM_ID_CONFIGURE parse it
        } else if (parsed_id == STRATUM_ID_CONFIGURE) {
            cJSON * mask = cJSON_GetObjectItem(result_json, "version-rolling.mask");
            if (mask != NULL) {
                result = STRATUM_RESULT_VERSION_MASK;
                message->version_mask = strtoul(mask->valuestring, NULL, 16);
            } else {
                ESP_LOGI(TAG, "error setting version mask: %s", stratum_json);
            }

        } else {
            ESP_LOGI(TAG, "unhandled result in stratum message: %s", stratum_json);
        }
    }

    message->method = result;

    if (message->method == MINING_NOTIFY) {

        mining_notify * new_work = malloc(sizeof(mining_notify));
        // new_work->difficulty = difficulty;
        cJSON * params = cJSON_GetObjectItem(json, "params");
        new_work->job_id = strdup(cJSON_GetArrayItem(params, 0)->valuestring);
        new_work->prev_block_hash = strdup(cJSON_GetArrayItem(params, 1)->valuestring);
        new_work->coinbase_1 = strdup(cJSON_GetArrayItem(params, 2)->valuestring);
        new_work->coinbase_2 = strdup(cJSON_GetArrayItem(params, 3)->valuestring);

        cJSON * merkle_branch = cJSON_GetArrayItem(params, 4);
        new_work->n_merkle_branches = cJSON_GetArraySize(merkle_branch);
        if (new_work->n_merkle_branches > MAX_MERKLE_BRANCHES) {
            printf("Too many Merkle branches.\n");
            abort();
        }
        new_work->merkle_branches = malloc(HASH_SIZE * new_work->n_merkle_branches);
        for (size_t i = 0; i < new_work->n_merkle_branches; i++) {
            hex2bin(cJSON_GetArrayItem(merkle_branch, i)->valuestring, new_work->merkle_branches + HASH_SIZE * i, HASH_SIZE);
        }

        new_work->version = strtoul(cJSON_GetArrayItem(params, 5)->valuestring, NULL, 16);
        new_work->target = strtoul(cJSON_GetArrayItem(params, 6)->valuestring, NULL, 16);
        new_work->ntime = strtoul(cJSON_GetArrayItem(params, 7)->valuestring, NULL, 16);

        message->mining_notification = new_work;

        // params can be varible length
        int paramsLength = cJSON_GetArraySize(params);
        int value = cJSON_IsTrue(cJSON_GetArrayItem(params, paramsLength - 1));
        message->should_abandon_work = value;
    } else if (message->method == MINING_SET_DIFFICULTY) {
        cJSON * params = cJSON_GetObjectItem(json, "params");
        uint32_t difficulty = cJSON_GetArrayItem(params, 0)->valueint;
        message->new_difficulty = difficulty;
    } else if (message->method == MINING_SET_VERSION_MASK) {
        cJSON * params = cJSON_GetObjectItem(json, "params");
        uint32_t version_mask = strtoul(cJSON_GetArrayItem(params, 0)->valuestring, NULL, 16);
        message->version_mask = version_mask;
    } else if (message->method == MINING_SET_EXTRANONCE) {
        cJSON * params = cJSON_GetObjectItem(json, "params");
        char * extranonce_str = cJSON_GetArrayItem(params, 0)->valuestring;
        uint32_t extranonce_2_len = cJSON_GetArrayItem(params, 1)->valueint;
        if (extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
            ESP_LOGW(TAG, "Extranonce_2_len %u exceeds maximum %d, clamping to maximum", 
                     extranonce_2_len, MAX_EXTRANONCE_2_LEN);
            extranonce_2_len = MAX_EXTRANONCE_2_LEN;
        }
        message->extranonce_str = strdup(extranonce_str);
        message->extranonce_2_len = extranonce_2_len;
    }
    done:
    cJSON_Delete(json);
}

void STRATUM_V1_free_mining_notify(mining_notify * params)
{
    free(params->job_id);
    free(params->prev_block_hash);
    free(params->coinbase_1);
    free(params->coinbase_2);
    free(params->merkle_branches);
    free(params);
}

int _parse_stratum_subscribe_result_message(const char * result_json_str, char ** extranonce, int * extranonce2_len)
{
    cJSON * root = cJSON_Parse(result_json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Unable to parse %s", result_json_str);
        return -1;
    }
    cJSON * result = cJSON_GetObjectItem(root, "result");
    if (result == NULL) {
        ESP_LOGE(TAG, "Unable to parse subscribe result %s", result_json_str);
        return -1;
    }

    cJSON * extranonce2_len_json = cJSON_GetArrayItem(result, 2);
    if (extranonce2_len_json == NULL) {
        ESP_LOGE(TAG, "Unable to parse extranonce2_len: %s", result->valuestring);
        return -1;
    }
    *extranonce2_len = extranonce2_len_json->valueint;

    cJSON * extranonce_json = cJSON_GetArrayItem(result, 1);
    if (extranonce_json == NULL) {
        ESP_LOGE(TAG, "Unable parse extranonce: %s", result->valuestring);
        return -1;
    }
    *extranonce = strdup(extranonce_json->valuestring);

    cJSON_Delete(root);

    return 0;
}

int STRATUM_V1_subscribe(int socket, int send_uid, const char * model)
{
    // Subscribe
    char subscribe_msg[BUFFER_SIZE];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *version = app_desc->version;	
    sprintf(subscribe_msg, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\"bitaxe/%s/%s\"]}\n", send_uid, model, version);
    debug_stratum_tx(subscribe_msg);

    return write(socket, subscribe_msg, strlen(subscribe_msg));
}

int STRATUM_V1_suggest_difficulty(int socket, int send_uid, uint32_t difficulty)
{
    char difficulty_msg[BUFFER_SIZE];
    sprintf(difficulty_msg, "{\"id\": %d, \"method\": \"mining.suggest_difficulty\", \"params\": [%ld]}\n", send_uid, difficulty);
    debug_stratum_tx(difficulty_msg);

    return write(socket, difficulty_msg, strlen(difficulty_msg));
}

int STRATUM_V1_extranonce_subscribe(int socket, int send_uid)
{
    char extranonce_msg[BUFFER_SIZE];
    sprintf(extranonce_msg, "{\"id\": %d, \"method\": \"mining.extranonce.subscribe\", \"params\": []}\n", send_uid);
    debug_stratum_tx(extranonce_msg);

    return write(socket, extranonce_msg, strlen(extranonce_msg));
}

int STRATUM_V1_authorize(int socket, int send_uid, const char * username, const char * pass)
{
    char authorize_msg[BUFFER_SIZE];
    sprintf(authorize_msg, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}\n", send_uid, username,
            pass);
    debug_stratum_tx(authorize_msg);

    return write(socket, authorize_msg, strlen(authorize_msg));
}

/// @param socket Socket to write to
/// @param send_uid Message ID
/// @param username The client’s user name.
/// @param job_id The job ID for the work being submitted.
/// @param extranonce_2 The hex-encoded value of extra nonce 2.
/// @param ntime The hex-encoded time value use in the block header.
/// @param nonce The hex-encoded nonce value to use in the block header.
/// @param version_bits The hex-encoded version bits set by miner (BIP310).
int STRATUM_V1_submit_share(int socket, int send_uid, const char * username, const char * job_id,
                            const char * extranonce_2, const uint32_t ntime,
                            const uint32_t nonce, const uint32_t version_bits)
{
    char submit_msg[BUFFER_SIZE];
    sprintf(submit_msg,
            "{\"id\": %d, \"method\": \"mining.submit\", \"params\": [\"%s\", \"%s\", \"%s\", \"%08lx\", \"%08lx\", \"%08lx\"]}\n",
            send_uid, username, job_id, extranonce_2, ntime, nonce, version_bits);
    debug_stratum_tx(submit_msg);

    return write(socket, submit_msg, strlen(submit_msg));
}

int STRATUM_V1_configure_version_rolling(int socket, int send_uid, uint32_t * version_mask)
{
    char configure_msg[BUFFER_SIZE * 2];
    sprintf(configure_msg,
            "{\"id\": %d, \"method\": \"mining.configure\", \"params\": [[\"version-rolling\"], {\"version-rolling.mask\": "
            "\"ffffffff\"}]}\n",
            send_uid);
    debug_stratum_tx(configure_msg);

    return write(socket, configure_msg, strlen(configure_msg));
}

static void debug_stratum_tx(const char * msg)
{
    STRATUM_V1_stamp_tx(last_parsed_request_id);
    //remove the trailing newline
    char * newline = strchr(msg, '\n');
    if (newline != NULL) {
        *newline = '\0';
    }
    ESP_LOGI(TAG, "tx: %s", msg);

    //put it back!
    if (newline != NULL) {
        *newline = '\n';
    }
}
