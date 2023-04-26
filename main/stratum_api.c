#include "stratum_api.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "lwip/sockets.h"

#define BUFFER_SIZE 1024

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
        } else if (strcmp("mining.set_difficulty", method->valuestring) == 0) {
            output.method = MINING_SET_DIFFICULTY;
        } else {
            output.method = STRATUM_UNKNOWN;
        }
    }

    cJSON_Delete(json);
    return output;
}

int subscribe_to_stratum(int socket)
{
    // Subscribe
    char subscribe_msg[BUFFER_SIZE];
    sprintf(subscribe_msg, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": []}", send_uid++);
    write(socket, subscribe_msg, strlen(subscribe_msg));
    char * line;
    line = receive_jsonrpc_line(socket);
    free(line);

    return 1;
}