#ifndef BZM_BRIDGE_H
#define BZM_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

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
#define BZM_BRIDGE_VERSION_MAX_LENGTH 63

#define BZM_BRIDGE_PROTOCOL_MAJOR 1
#define BZM_BRIDGE_PROTOCOL_MINOR 0
#define BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION 0x01
#define BZM_BRIDGE_SAFETY_STATUS_LENGTH 17

#define BZM_BRIDGE_SAFETY_CAP_5V_CONTROL (1U << 0)
#define BZM_BRIDGE_SAFETY_CAP_ASIC_RESET_CONTROL (1U << 1)
#define BZM_BRIDGE_SAFETY_CAP_FAN_FORCE_FULL (1U << 2)
#define BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED (1U << 3)
#define BZM_BRIDGE_SAFETY_CAP_CORE_POWER_CUTOFF (1U << 4)
#define BZM_BRIDGE_SAFETY_CAP_FAN_TACH_INTERLOCK (1U << 5)
#define BZM_BRIDGE_SAFETY_CAP_INDEPENDENT_TRIP_MONITOR (1U << 6)
#define BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED (1U << 7)

#define BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE (1U << 0)
#define BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID (1U << 1)
#define BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR (1U << 2)
#define BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR (1U << 3)
#define BZM_BRIDGE_SAFETY_EVIDENCE_CORE_CUTOFF_AVAILABLE (1U << 4)
#define BZM_BRIDGE_SAFETY_EVIDENCE_FAN_TACH_INTERLOCK_AVAILABLE (1U << 5)
#define BZM_BRIDGE_SAFETY_EVIDENCE_INDEPENDENT_TRIP_MONITOR_AVAILABLE (1U << 6)

#define BZM_BRIDGE_GPIO_5V_ENABLE 0x01
#define BZM_BRIDGE_GPIO_ASIC_RESET 0x02
#define BZM_BRIDGE_FAN_SET_SPEED 0x10
#define BZM_BRIDGE_FAN_GET_TACH 0x20

#define BZM_BRIDGE_MAX_REQUEST_SIZE 64
#define BZM_BRIDGE_MAX_RESPONSE_SIZE 260

typedef struct {
    uint8_t schema_version;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    char version[BZM_BRIDGE_VERSION_MAX_LENGTH + 1];
} bzm_bridge_info_t;

typedef enum {
    BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE = 0,
    BZM_BRIDGE_SAFETY_STAGE_LEASE = 1,
    BZM_BRIDGE_SAFETY_STAGE_TRIP_LATCH = 2,
} bzm_bridge_safety_stage_t;

typedef enum {
    BZM_BRIDGE_SAFETY_STATE_SAFE_OFF = 0,
    BZM_BRIDGE_SAFETY_STATE_CONTROLLED = 1,
    BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED = 2,
} bzm_bridge_safety_state_t;

typedef enum {
    BZM_BRIDGE_SAFETY_FAULT_NONE = 0,
    BZM_BRIDGE_SAFETY_FAULT_LEASE_EXPIRED = 1,
    BZM_BRIDGE_SAFETY_FAULT_ASIC_TRIP = 2,
    BZM_BRIDGE_SAFETY_FAULT_STATUS_INVALID = 0xff,
} bzm_bridge_safety_fault_t;

typedef enum {
    BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF = 0,
    BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED = 1,
    BZM_BRIDGE_SAFETY_RUNTIME_BAD_FAULT = 0x80,
    BZM_BRIDGE_SAFETY_RUNTIME_BAD_LEASE = 0x81,
    BZM_BRIDGE_SAFETY_RUNTIME_BAD_TRIP_INPUT = 0x82,
    BZM_BRIDGE_SAFETY_RUNTIME_BAD_UNSAFE_OUTPUTS = 0x83,
} bzm_bridge_safety_runtime_verdict_t;

typedef enum {
    BZM_BRIDGE_SAFETY_PRODUCTION_GOOD = 0,
    BZM_BRIDGE_SAFETY_PRODUCTION_BAD_STAGE_DISABLED = 0x80,
    BZM_BRIDGE_SAFETY_PRODUCTION_BAD_CAPABILITY_GAP = 0x81,
    BZM_BRIDGE_SAFETY_PRODUCTION_BAD_RUNTIME = 0x82,
} bzm_bridge_safety_production_verdict_t;

/*
 * A status is usable only when the command returned ESP_OK and valid is true.
 * Decode and transport failures overwrite the caller's status with a
 * valid=false, fault-shaped sentinel so stale good evidence cannot survive.
 */
typedef struct {
    bool valid;
    uint8_t schema_version;
    bzm_bridge_safety_stage_t stage;
    bzm_bridge_safety_state_t state;
    bzm_bridge_safety_fault_t fault;
    bzm_bridge_safety_runtime_verdict_t runtime_verdict;
    bzm_bridge_safety_production_verdict_t production_verdict;
    uint16_t capabilities;
    uint16_t evidence;
    uint32_t lease_remaining_ms;
    bool five_volt_enabled;
    bool asic_reset_asserted;
    bool fan_full;
    uint8_t fan_percent;
    bool trip_input_asserted;
} bzm_bridge_safety_status_t;

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
bool bzm_bridge_info_supports_safety(const bzm_bridge_info_t *info);
/* Pure protocol decoder: requires the exact schema-1 17-byte payload. */
esp_err_t bzm_bridge_decode_safety_status(
    const uint8_t *payload, size_t payload_length,
    bzm_bridge_safety_status_t *status);
/* A latched bridge fault may be cleared only after physical outputs already
 * read safe and the live trip input has deasserted. Full SAFE_OFF evidence is
 * deliberately re-proved after the clear command. */
bool bzm_bridge_safety_status_allows_fault_clear(
    const bzm_bridge_safety_status_t *status);

esp_err_t BZM_bridge_init(void);
esp_err_t BZM_bridge_get_info(bzm_bridge_info_t *info);
esp_err_t BZM_bridge_get_safety_status(bzm_bridge_safety_status_t *status);
esp_err_t BZM_bridge_arm_safety(bzm_bridge_safety_status_t *status);
esp_err_t BZM_bridge_safety_heartbeat(bzm_bridge_safety_status_t *status);
esp_err_t BZM_bridge_clear_safety_fault(bzm_bridge_safety_status_t *status);
esp_err_t BZM_bridge_disarm_safety(bzm_bridge_safety_status_t *status);
esp_err_t BZM_bridge_set_5v_enabled(bool enabled);
esp_err_t BZM_bridge_set_asic_reset(bool high);
esp_err_t BZM_bridge_pulse_asic_reset(void);
esp_err_t BZM_bridge_set_fan_percent(float percent);
esp_err_t BZM_bridge_get_fan_rpm(uint16_t *rpm);

#endif // BZM_BRIDGE_H
