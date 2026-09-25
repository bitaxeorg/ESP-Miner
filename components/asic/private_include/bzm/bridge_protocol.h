#ifndef BZM_BRIDGE_PROTOCOL_H
#define BZM_BRIDGE_PROTOCOL_H

#include "bzm/bridge.h"

#define BZM_BRIDGE_CONTROL_BAUD_RATE 115200
#define BZM_BRIDGE_CONTROL_TX_GPIO 43
#define BZM_BRIDGE_CONTROL_RX_GPIO 44
#define BZM_BRIDGE_PAGE_GPIO 0x06
#define BZM_BRIDGE_PAGE_FAN 0x09
#define BZM_BRIDGE_PAGE_SYSTEM 0x00
#define BZM_BRIDGE_SYSTEM_GET_INFO 0x01
#define BZM_BRIDGE_SYSTEM_GET_SAFETY_STATUS 0x10
#define BZM_BRIDGE_SYSTEM_ARM_SAFETY_LEASE 0x11
#define BZM_BRIDGE_SYSTEM_SAFETY_HEARTBEAT 0x12
#define BZM_BRIDGE_SYSTEM_CLEAR_SAFETY_FAULT 0x13
#define BZM_BRIDGE_SYSTEM_DISARM_SAFETY_LEASE 0x14
#define BZM_BRIDGE_INFO_SCHEMA_VERSION 0x01
#define BZM_BRIDGE_PROTOCOL_MAJOR 1
#define BZM_BRIDGE_PROTOCOL_MINOR 0
#define BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION 0x01
#define BZM_BRIDGE_SAFETY_STATUS_LENGTH 17
#define BZM_BRIDGE_GPIO_5V_ENABLE 0x01
#define BZM_BRIDGE_GPIO_ASIC_RESET 0x02
#define BZM_BRIDGE_FAN_SET_SPEED 0x10
#define BZM_BRIDGE_FAN_GET_TACH 0x20
#define BZM_BRIDGE_MAX_REQUEST_SIZE 64
#define BZM_BRIDGE_MAX_RESPONSE_SIZE 260

size_t bzm_bridge_encode_request(uint8_t id, uint8_t page, uint8_t command,
                                 const uint8_t *payload,
                                 size_t payload_length, uint8_t *encoded,
                                 size_t encoded_capacity);
esp_err_t bzm_bridge_decode_response(uint8_t expected_id,
                                     const uint8_t *frame,
                                     size_t frame_length,
                                     const uint8_t **payload,
                                     size_t *payload_length);
esp_err_t bzm_bridge_decode_info(const uint8_t *payload,
                                 size_t payload_length,
                                 bzm_bridge_info_t *info);
esp_err_t bzm_bridge_decode_safety_status(
    const uint8_t *payload, size_t payload_length,
    bzm_bridge_safety_status_t *status);

#endif // BZM_BRIDGE_PROTOCOL_H
