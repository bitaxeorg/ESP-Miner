#include <string.h>

#include "bzm_bridge.h"
#include "unity.h"

static void assert_safety_status_rejected(const uint8_t *payload,
                                          size_t payload_length)
{
    bzm_bridge_safety_status_t status;
    memset(&status, 0xa5, sizeof(status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      bzm_bridge_decode_safety_status(
                          payload, payload_length, &status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED,
                      status.state);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_FAULT_STATUS_INVALID,
                      status.fault);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_RUNTIME_BAD_FAULT,
                      status.runtime_verdict);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_PRODUCTION_BAD_RUNTIME,
                      status.production_verdict);
}

TEST_CASE("Bonanza bridge encodes the RP2040 control packet format",
          "[asic][bzm][bridge][qemu-integration]")
{
    uint8_t frame[16];
    uint8_t level = 1;
    size_t length = bzm_bridge_encode_request(
        0x2a, BZM_BRIDGE_PAGE_GPIO, BZM_BRIDGE_GPIO_5V_ENABLE,
        &level, 1, frame, sizeof(frame));
    const uint8_t expected[] = {
        0x07, 0x00, 0x2a, 0x00, 0x06, 0x01, 0x01,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, sizeof(expected));

    TEST_ASSERT_EQUAL(0, bzm_bridge_encode_request(
        0, 0, 0, NULL, 1, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(0, bzm_bridge_encode_request(
        0, 0, 0, &level, 1, frame, 6));
}

TEST_CASE("Bonanza bridge validates response length id and error frames",
          "[asic][bzm][bridge][qemu-integration]")
{
    const uint8_t *payload;
    size_t payload_length;
    uint8_t response[] = {0x05, 0x00, 0x2a, 0x34, 0x12};
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_response(
        0x2a, response, sizeof(response), &payload, &payload_length));
    TEST_ASSERT_EQUAL_UINT32(2, payload_length);
    TEST_ASSERT_EQUAL_HEX8(0x34, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, payload[1]);

    response[2] = 0x2b;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, bzm_bridge_decode_response(
        0x2a, response, sizeof(response), &payload, &payload_length));
    response[2] = 0x2a;
    response[0] = 4;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, bzm_bridge_decode_response(
        0x2a, response, sizeof(response), &payload, &payload_length));

    uint8_t timeout[] = {0x04, 0x00, 0x2a, 0x10};
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bzm_bridge_decode_response(
        0x2a, timeout, sizeof(timeout), &payload, &payload_length));
    uint8_t invalid[] = {0x04, 0x00, 0x2a, 0x11};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, bzm_bridge_decode_response(
        0x2a, invalid, sizeof(invalid), &payload, &payload_length));
    uint8_t denied[] = {0x04, 0x00, 0x2a, 0x12};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bzm_bridge_decode_response(
        0x2a, denied, sizeof(denied), &payload, &payload_length));
    uint8_t fault[] = {0x04, 0x00, 0x2a, 0x13};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bzm_bridge_decode_response(
        0x2a, fault, sizeof(fault), &payload, &payload_length));
}

TEST_CASE("Bonanza bridge decodes bounded version information",
          "[asic][bzm][bridge][version][qemu-integration]")
{
    const uint8_t payload[] = {
        BZM_BRIDGE_INFO_SCHEMA_VERSION,
        BZM_BRIDGE_PROTOCOL_MAJOR, BZM_BRIDGE_PROTOCOL_MINOR, 13,
        '0', '.', '0', '.', '1', '+', 'g', '9', '7', 'e', '7', '7', 'f',
    };
    bzm_bridge_info_t info;
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_info(
        payload, sizeof(payload), &info));
    TEST_ASSERT_EQUAL_UINT8(BZM_BRIDGE_INFO_SCHEMA_VERSION,
                            info.schema_version);
    TEST_ASSERT_EQUAL_UINT8(BZM_BRIDGE_PROTOCOL_MAJOR, info.protocol_major);
    TEST_ASSERT_EQUAL_UINT8(BZM_BRIDGE_PROTOCOL_MINOR, info.protocol_minor);
    TEST_ASSERT_EQUAL_STRING("0.0.1+g97e77f", info.version);

    uint8_t bad_length[sizeof(payload)];
    memcpy(bad_length, payload, sizeof(payload));
    bad_length[3]--;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, bzm_bridge_decode_info(
        bad_length, sizeof(bad_length), &info));

    uint8_t bad_character[sizeof(payload)];
    memcpy(bad_character, payload, sizeof(payload));
    bad_character[4] = '\n';
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, bzm_bridge_decode_info(
        bad_character, sizeof(bad_character), &info));

    TEST_ASSERT_TRUE(bzm_bridge_info_supports_safety(&info));
    info.protocol_major++;
    TEST_ASSERT_FALSE(bzm_bridge_info_supports_safety(&info));
    info.protocol_major = BZM_BRIDGE_PROTOCOL_MAJOR;
    info.schema_version++;
    TEST_ASSERT_FALSE(bzm_bridge_info_supports_safety(&info));
    TEST_ASSERT_FALSE(bzm_bridge_info_supports_safety(NULL));
}

TEST_CASE("Bonanza bridge decodes coherent safety status evidence",
          "[asic][bzm][bridge][safety][qemu-integration]")
{
    const uint8_t boot_safe[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x8f, 0x00, 0x0f, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 100, 0x00,
    };
    bzm_bridge_safety_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_safety_status(
        boot_safe, sizeof(boot_safe), &status));
    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL_UINT8(BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION,
                            status.schema_version);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE, status.stage);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_STATE_SAFE_OFF, status.state);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_FAULT_NONE, status.fault);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF,
                      status.runtime_verdict);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_PRODUCTION_BAD_STAGE_DISABLED,
                      status.production_verdict);
    TEST_ASSERT_EQUAL_HEX16(0x008f, status.capabilities);
    TEST_ASSERT_EQUAL_HEX16(0x000f, status.evidence);
    TEST_ASSERT_EQUAL_UINT32(0, status.lease_remaining_ms);
    TEST_ASSERT_FALSE(status.five_volt_enabled);
    TEST_ASSERT_TRUE(status.asic_reset_asserted);
    TEST_ASSERT_TRUE(status.fan_full);
    TEST_ASSERT_EQUAL_UINT8(100, status.fan_percent);
    TEST_ASSERT_FALSE(status.trip_input_asserted);

    const uint8_t leased_running[] = {
        0x01, 0x01, 0x01, 0x00, 0x01, 0x80,
        0x8f, 0x00, 0x0e, 0x00,
        0xe8, 0x03, 0x00, 0x00,
        0x05, 100, 0x00,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_safety_status(
        leased_running, sizeof(leased_running), &status));
    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_STAGE_LEASE, status.stage);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_STATE_CONTROLLED, status.state);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED,
                      status.runtime_verdict);
    TEST_ASSERT_EQUAL_UINT32(1000, status.lease_remaining_ms);
    TEST_ASSERT_TRUE(status.five_volt_enabled);
    TEST_ASSERT_FALSE(status.asic_reset_asserted);

    const uint8_t tripped[] = {
        0x01, 0x02, 0x02, 0x02, 0x80, 0x82,
        0x8f, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 100, 0x01,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_safety_status(
        tripped, sizeof(tripped), &status));
    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED,
                      status.state);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_FAULT_ASIC_TRIP, status.fault);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_RUNTIME_BAD_FAULT,
                      status.runtime_verdict);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_PRODUCTION_BAD_RUNTIME,
                      status.production_verdict);
    TEST_ASSERT_TRUE(status.trip_input_asserted);

    const uint8_t capability_gap[] = {
        0x01, 0x02, 0x00, 0x00, 0x00, 0x81,
        0x8f, 0x00, 0x0d, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 100, 0x00,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_safety_status(
        capability_gap, sizeof(capability_gap), &status));
    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_PRODUCTION_BAD_CAPABILITY_GAP,
                      status.production_verdict);

    const uint8_t production_ready[] = {
        0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x00, 0x7d, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 100, 0x00,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bzm_bridge_decode_safety_status(
        production_ready, sizeof(production_ready), &status));
    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL(BZM_BRIDGE_SAFETY_PRODUCTION_GOOD,
                      status.production_verdict);
}

TEST_CASE("Bonanza bridge rejects malformed or contradictory safety status",
          "[asic][bzm][bridge][safety][qemu-integration]")
{
    const uint8_t valid[] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x8f, 0x00, 0x0f, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 100, 0x00,
    };
    uint8_t malformed[sizeof(valid) + 1];

    assert_safety_status_rejected(valid, sizeof(valid) - 1);
    memcpy(malformed, valid, sizeof(valid));
    malformed[sizeof(valid)] = 0;
    assert_safety_status_rejected(malformed, sizeof(malformed));

#define REJECT_MUTATION(offset, value) do { \
        memcpy(malformed, valid, sizeof(valid)); \
        malformed[(offset)] = (value); \
        assert_safety_status_rejected(malformed, sizeof(valid)); \
    } while (0)

    REJECT_MUTATION(0, 2);       // Unknown schema.
    REJECT_MUTATION(1, 3);       // Unknown stage.
    REJECT_MUTATION(2, 3);       // Unknown state.
    REJECT_MUTATION(3, 3);       // Unknown fault.
    REJECT_MUTATION(4, 0x84);    // Unknown runtime verdict.
    REJECT_MUTATION(5, 0x83);    // Unknown production verdict.
    REJECT_MUTATION(7, 0x80);    // Unknown capability.
    REJECT_MUTATION(9, 0x80);    // Unknown evidence.
    REJECT_MUTATION(14, 0x86);   // Reserved output flag.
    REJECT_MUTATION(14, 0x02);   // Fan-full flag contradicts 100 percent.
    REJECT_MUTATION(15, 101);    // Invalid fan percentage.
    REJECT_MUTATION(16, 2);      // Invalid trip boolean.
    REJECT_MUTATION(8, 0x0e);    // Outputs-safe evidence contradicts outputs.
    REJECT_MUTATION(8, 0x0d);    // Stage-zero lease-valid evidence missing.
    REJECT_MUTATION(8, 0x0b);    // Trip-clear evidence contradicts trip.
    REJECT_MUTATION(8, 0x07);    // Fault-clear evidence contradicts fault.
    REJECT_MUTATION(8, 0x1f);    // Core-cutoff evidence lacks capability.
    REJECT_MUTATION(14, 0x07);   // Safe-off state reports 5 V enabled.
    REJECT_MUTATION(4, 0x01);    // Runtime verdict contradicts safe-off.
    REJECT_MUTATION(5, 0x00);    // Production verdict ignores disabled stage.

#undef REJECT_MUTATION

    bzm_bridge_safety_status_t status;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bzm_bridge_decode_safety_status(
                          NULL, sizeof(valid), &status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bzm_bridge_decode_safety_status(
                          valid, sizeof(valid), NULL));
}

TEST_CASE("Bonanza bridge command constants encode every owned peripheral",
          "[asic][bzm][bridge][qemu-integration]")
{
    typedef struct {
        uint8_t page;
        uint8_t command;
    } command_t;
    const command_t commands[] = {
        {BZM_BRIDGE_PAGE_GPIO, BZM_BRIDGE_GPIO_5V_ENABLE},
        {BZM_BRIDGE_PAGE_GPIO, BZM_BRIDGE_GPIO_ASIC_RESET},
        {BZM_BRIDGE_PAGE_FAN, BZM_BRIDGE_FAN_SET_SPEED},
        {BZM_BRIDGE_PAGE_FAN, BZM_BRIDGE_FAN_GET_TACH},
        {BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_GET_INFO},
        {BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_GET_SAFETY_STATUS},
        {BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_ARM_SAFETY_LEASE},
        {BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_SAFETY_HEARTBEAT},
        {BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_CLEAR_SAFETY_FAULT},
        {BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_DISARM_SAFETY_LEASE},
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        uint8_t frame[8];
        TEST_ASSERT_EQUAL_UINT32(6, bzm_bridge_encode_request(
            i, commands[i].page, commands[i].command, NULL, 0,
            frame, sizeof(frame)));
        TEST_ASSERT_EQUAL_HEX8(commands[i].page, frame[4]);
        TEST_ASSERT_EQUAL_HEX8(commands[i].command, frame[5]);
    }
}

TEST_CASE("Bonanza bridge operations fail closed while unavailable",
          "[asic][bzm][bridge][unavailable][qemu-integration]")
{
    uint16_t rpm = 0;
    bzm_bridge_info_t info;
    bzm_bridge_safety_status_t status;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_set_5v_enabled(true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_set_asic_reset(true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_pulse_asic_reset());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_set_fan_percent(1.0f));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_get_fan_rpm(&rpm));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_get_info(&info));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_get_safety_status(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_arm_safety(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_safety_heartbeat(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_clear_safety_fault(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      BZM_bridge_disarm_safety(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      BZM_bridge_get_safety_status(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      BZM_bridge_set_fan_percent(-0.1f));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      BZM_bridge_set_fan_percent(1.1f));
}

TEST_CASE("Bonanza bridge fault clear requires physical safe outputs and trip low",
          "[asic][bzm][bridge][safety][qemu-integration]")
{
    bzm_bridge_safety_status_t status = {
        .valid = true,
        .state = BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED,
        .fault = BZM_BRIDGE_SAFETY_FAULT_ASIC_TRIP,
        .five_volt_enabled = false,
        .asic_reset_asserted = true,
        .fan_full = true,
        .fan_percent = 100,
        .trip_input_asserted = false,
    };
    TEST_ASSERT_TRUE(bzm_bridge_safety_status_allows_fault_clear(&status));

    status.five_volt_enabled = true;
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(&status));
    status.five_volt_enabled = false;
    status.asic_reset_asserted = false;
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(&status));
    status.asic_reset_asserted = true;
    status.fan_full = false;
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(&status));
    status.fan_full = true;
    status.fan_percent = 99;
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(&status));
    status.fan_percent = 100;
    status.trip_input_asserted = true;
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(&status));
    status.trip_input_asserted = false;
    status.valid = false;
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(&status));
    TEST_ASSERT_FALSE(bzm_bridge_safety_status_allows_fault_clear(NULL));
}
