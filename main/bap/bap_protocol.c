/**
 * @file bap_protocol.c
 * @brief BAP (Bitaxe accessory protocol) core protocol utilities
 * 
 * Contains protocol-level functions for message formatting, checksums,
 * and string conversions between enums and strings.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "bap_protocol.h"

static const char *TAG = "BAP_PROTOCOL";

static const char *parameter_strings[] = {
    "systemInfo",
    "hashrate",
    "temperature",
    "power",
    "voltage",
    "current",
    "shares",
    "frequency",
    "asic_voltage",
    "ssid",
    "password",
    "fan_speed",
    "auto_fan",
    "best_difficulty",
    "block_height",
    "wifi",
    "found_block",
    "show_new_block",
    "unknown"
};

static const char *command_strings[] = {
    "REQ", "RES", "SUB", "UNSUB", "SET", "ACK", "ERR", "CMD", "STA", "LOG"
};

bap_parameter_t BAP_parameter_from_string(const char *param_str) {
    for (int i = 0; i < BAP_PARAM_UNKNOWN; ++i) {
        if (strcmp(param_str, parameter_strings[i]) == 0) {
            return (bap_parameter_t)i;
        }
    }
    return BAP_PARAM_UNKNOWN;
}

const char* BAP_parameter_to_string(bap_parameter_t param) {
    if (param >= 0 && param < BAP_PARAM_UNKNOWN) {
        return parameter_strings[param];
    }
    return "unknown";
}

bap_command_t BAP_command_from_string(const char *cmd_str) {
    for (int i = 0; i < BAP_CMD_UNKNOWN; ++i) {
        if (strcmp(cmd_str, command_strings[i]) == 0) {
            return (bap_command_t)i;
        }
    }
    return BAP_CMD_UNKNOWN;
}

const char* BAP_command_to_string(bap_command_t cmd) {
    if (cmd >= 0 && cmd < BAP_CMD_UNKNOWN) {
        return command_strings[cmd];
    }
    return "UNK";
}

uint8_t BAP_calculate_checksum(const char *sentence_body) {
    uint8_t checksum = 0;
    //ESP_LOGI(TAG, "Calculating checksum for: %s", sentence_body);
    
    while (*sentence_body) {
        ESP_LOGD(TAG, "XOR: 0x%02X ^ 0x%02X = 0x%02X",
                checksum, (uint8_t)(*sentence_body),
                checksum ^ (uint8_t)(*sentence_body));
        checksum ^= (uint8_t)(*sentence_body++);
    }
    
    //ESP_LOGI(TAG, "Final checksum: 0x%02X", checksum);
    return checksum;
}

bool BAP_format_message(char *message, size_t message_size, bap_command_t cmd,
                        const char *parameter, const char *value, size_t *message_len) {
    char sentence_body[BAP_MAX_MESSAGE_LEN];
    int formatted_len;

    if (message_len == NULL) {
        return false;
    }
    *message_len = 0;

    if (message == NULL || message_size == 0 || parameter == NULL) {
        return false;
    }

    message[0] = '\0';

    if (value != NULL && value[0] != '\0') {
        formatted_len = snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s,%s",
                                 BAP_command_to_string(cmd), parameter, value);
    } else {
        formatted_len = snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s",
                                 BAP_command_to_string(cmd), parameter);
    }

    if (formatted_len < 0 || (size_t)formatted_len >= sizeof(sentence_body)) {
        return false;
    }

    uint8_t checksum = BAP_calculate_checksum(sentence_body);
    formatted_len = snprintf(message, message_size, "$%s*%02X\r\n", sentence_body, checksum);
    if (formatted_len < 0 || (size_t)formatted_len >= message_size) {
        message[0] = '\0';
        return false;
    }

    *message_len = (size_t)formatted_len;
    return true;
}
