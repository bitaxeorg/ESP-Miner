#include "stratum_api.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "lwip/sockets.h"


#define BUFFER_SIZE 1024
static const char *TAG = "stratum client";

static char *json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;

static int send_uid = 1;

void initialize_stratum_buffer()
{
    json_rpc_buffer = malloc(BUFFER_SIZE); // Allocate memory dynamically
    json_rpc_buffer_size = BUFFER_SIZE;
    memset(json_rpc_buffer, 0, BUFFER_SIZE);
    if (json_rpc_buffer == NULL)
    {
        printf("Error: Failed to allocate memory for buffer\n");
        exit(1); // Handle error case
    }
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

    if (new < json_rpc_buffer_size)
    {
        return;
    }

    new = new + (BUFFER_SIZE - (new % BUFFER_SIZE));
    void *new_sockbuf = realloc(json_rpc_buffer, new);

    if (new_sockbuf == NULL)
    {
        fprintf(stderr, "Error: realloc failed in recalloc_sock()\n");
        exit(EXIT_FAILURE);
    }

    json_rpc_buffer = new_sockbuf;
    memset(json_rpc_buffer + old, 0, new - old);
    json_rpc_buffer_size = new;
}

char * receive_jsonrpc_line(int sockfd)
{
    if (json_rpc_buffer == NULL) {
        initialize_stratum_buffer();
    }
    char *line, *tok = NULL;
    char recv_buffer[BUFFER_SIZE];
    int nbytes;
    size_t buflen = 0;

    if (!strstr(json_rpc_buffer, "\n"))
    {
        do
        {
            memset(recv_buffer, 0, BUFFER_SIZE);
            nbytes = recv(sockfd, recv_buffer, BUFFER_SIZE - 1, 0);
            if (nbytes == -1)
            {
                perror("recv");
                exit(EXIT_FAILURE);
            }

            realloc_json_buffer(nbytes);
            strncat(json_rpc_buffer, recv_buffer, nbytes);
            ESP_LOGI(TAG, "Current buffer %s", json_rpc_buffer);
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

static uint8_t hex2val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        return 0;
    }
}

static size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len)
{
    size_t len = 0;

    while (*hex && len < bin_len) {
        bin[len] = hex2val(*hex++) << 4;

        if (!*hex) {
            len++;
            break;
        }

        bin[len++] |= hex2val(*hex++);
    }

    return len;
}

work parse_notify_work(cJSON * params) {
    work new_work;
    new_work.job_id = (uint32_t) strtoul(cJSON_GetArrayItem(params, 0)->valuestring, NULL, 16);
    hex2bin(cJSON_GetArrayItem(params, 1)->valuestring, new_work.prev_block_hash, PREV_BLOCK_HASH_SIZE * 2);

    char *coinb1 = cJSON_GetArrayItem(params, 2)->valuestring;
    char *coinb2 = cJSON_GetArrayItem(params, 3)->valuestring;
    hex2bin(coinb1, new_work.coinbase_1, strlen(coinb1) / 2);
    new_work.coinbase_1_len = strlen(coinb1) / 2;
    hex2bin(coinb2, new_work.coinbase_2, strlen(coinb2) / 2);
    new_work.coinbase_2_len = strlen(coinb2) / 2;

    cJSON *merkle_branch = cJSON_GetArrayItem(params, 4);
    new_work.n_merkle_branches = cJSON_GetArraySize(merkle_branch);
    if (new_work.n_merkle_branches > MAX_MERKLE_BRANCHES) {
        printf("Too many Merkle branches.\n");
        abort();
    }
    for (size_t i = 0; i < new_work.n_merkle_branches; i++) {
        hex2bin(cJSON_GetArrayItem(merkle_branch, i)->valuestring, new_work.merkle_branches[i], PREV_BLOCK_HASH_SIZE * 2);
    }

    return new_work;
}

stratum_message parse_stratum_notify_message(const char * stratum_json)
{
    stratum_message output;
    output.method = STRATUM_UNKNOWN;
    cJSON *json = cJSON_Parse(stratum_json);

    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *method = cJSON_GetObjectItem(json, "method");

    if (id != NULL && cJSON_IsNumber(id)) {
        output.id = id->valueint;
    }

    if (method != NULL && cJSON_IsString(method)) {
        output.method_str = strdup(method->valuestring);
        if (strcmp("mining.notify", method->valuestring) == 0) {
            output.method = MINING_NOTIFY;
            output.notify_work = parse_notify_work(cJSON_GetObjectItem(json, "params"));
        } else if (strcmp("mining.set_difficulty", method->valuestring) == 0) {
            output.method = MINING_SET_DIFFICULTY;
        } else {
            output.method = STRATUM_UNKNOWN;
        }
    }

    cJSON_Delete(json);
    return output;
}

int parse_stratum_subscribe_result_message(const char * result_json_str,
                                           char ** extranonce,
                                           int * extranonce2_len)
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
    *extranonce = malloc(strlen(extranonce_json->valuestring) + 1);
    strcpy(*extranonce, extranonce_json->valuestring);

    cJSON_Delete(root);

    return 0;
}

int subscribe_to_stratum(int socket, char ** extranonce, int * extranonce2_len)
{
    // Subscribe
    char subscribe_msg[BUFFER_SIZE];
    sprintf(subscribe_msg, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": []}\n", send_uid++);
    ESP_LOGI(TAG, "Subscribe: %s", subscribe_msg);
    write(socket, subscribe_msg, strlen(subscribe_msg));
    char * line;
    line = receive_jsonrpc_line(socket);

    ESP_LOGI(TAG, "Received result %s", line);

    parse_stratum_subscribe_result_message(line, extranonce, extranonce2_len);

    free(line);

    return 1;
}

int auth_to_stratum(int socket, const char * username)
{
    char authorize_msg[BUFFER_SIZE];
    sprintf(authorize_msg, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"x\"]}\n",
            send_uid++, username);
    ESP_LOGI(TAG, "Authorize: %s", authorize_msg);
   
    write(socket, authorize_msg, strlen(authorize_msg));
    char * line;
    line = receive_jsonrpc_line(socket);

    ESP_LOGI(TAG, "Received result %s", line);
    
    // TODO: Parse authorize results

    free(line);

    return 1;
}