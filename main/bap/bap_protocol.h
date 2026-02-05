/**
 * @file bap_protocol.h
 * @brief BAP (Bitaxe accessory protocol) core protocol utilities
 * 
 * Contains protocol-level functions for message formatting, checksums,
 * and string conversions between enums and strings.
 */

#ifndef BAP_PROTOCOL_H_
#define BAP_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAP_MAX_MESSAGE_LEN 256

// BAP Command Types
typedef enum {
    BAP_CMD_REQ,
    BAP_CMD_RES,
    BAP_CMD_SUB,
    BAP_CMD_UNSUB,
    BAP_CMD_SET,
    BAP_CMD_ACK,
    BAP_CMD_ERR,
    BAP_CMD_CMD,
    BAP_CMD_STA,
    BAP_CMD_LOG,
    BAP_CMD_UNKNOWN
} bap_command_t;

// Subscription parameter types
typedef enum {
    BAP_PARAM_SYSTEM_INFO,
    BAP_PARAM_HASHRATE,
    BAP_PARAM_TEMPERATURE,
    BAP_PARAM_POWER,
    BAP_PARAM_VOLTAGE,
    BAP_PARAM_CURRENT,
    BAP_PARAM_SHARES,
    BAP_PARAM_FREQUENCY,
    BAP_PARAM_ASIC_VOLTAGE,
    BAP_PARAM_SSID,
    BAP_PARAM_PASSWORD,
    BAP_PARAM_FAN_SPEED,
    BAP_PARAM_AUTO_FAN_SPEED,
    BAP_PARAM_BEST_DIFFICULTY,
    BAP_PARAM_BLOCK_HEIGHT,
    BAP_PARAM_WIFI,
    BAP_PARAM_UNKNOWN
} bap_parameter_t;

/**
 * @brief Convert parameter enum to string
 * @param param Parameter enum
 * @return Parameter string or "unknown" if invalid
 */
const char* BAP_parameter_to_string(bap_parameter_t param);

/**
 * @brief Convert parameter string to enum
 * @param param_str Parameter string
 * @return Parameter enum or BAP_PARAM_UNKNOWN if not found
 */
bap_parameter_t BAP_parameter_from_string(const char *param_str);

/**
 * @brief Convert command enum to string
 * @param cmd Command enum
 * @return Command string or "UNK" if unknown
 */
const char* BAP_command_to_string(bap_command_t cmd);

/**
 * @brief Convert command string to enum
 * @param cmd_str Command string
 * @return Command enum or BAP_CMD_UNKNOWN if not found
 */
bap_command_t BAP_command_from_string(const char *cmd_str);

/**
 * @brief Calculate XOR checksum for sentence body
 * @param sentence_body The sentence body (without $ and *)
 * @return Calculated checksum
 */
uint8_t BAP_calculate_checksum(const char *sentence_body);

#ifdef __cplusplus
}
#endif

#endif /* BAP_PROTOCOL_H_ */
