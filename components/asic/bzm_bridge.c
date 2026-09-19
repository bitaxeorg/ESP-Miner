#include "bzm_bridge.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BZM_BRIDGE_UART UART_NUM_2
#define BZM_BRIDGE_UART_BUFFER_SIZE 512

static const char *TAG = "bzm_bridge";
static pthread_mutex_t BRIDGE_LOCK = PTHREAD_MUTEX_INITIALIZER;
static bool INITIALIZED;
static uint8_t NEXT_COMMAND_ID;

size_t bzm_bridge_encode_request(uint8_t id, uint8_t page, uint8_t command,
                                 const uint8_t *payload,
                                 size_t payload_length, uint8_t *encoded,
                                 size_t encoded_capacity)
{
    size_t frame_length = payload_length + 6;
    if (encoded == NULL || frame_length > UINT16_MAX ||
        frame_length > encoded_capacity ||
        (payload_length != 0 && payload == NULL)) {
        return 0;
    }

    encoded[0] = frame_length & 0xff;
    encoded[1] = (frame_length >> 8) & 0xff;
    encoded[2] = id;
    encoded[3] = 0;
    encoded[4] = page;
    encoded[5] = command;
    if (payload_length != 0) {
        memcpy(encoded + 6, payload, payload_length);
    }
    return frame_length;
}

esp_err_t bzm_bridge_decode_response(uint8_t expected_id,
                                     const uint8_t *frame,
                                     size_t frame_length,
                                     const uint8_t **payload,
                                     size_t *payload_length)
{
    if (frame == NULL || payload == NULL || payload_length == NULL ||
        frame_length < 3) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t declared_length = (size_t)frame[0] | ((size_t)frame[1] << 8);
    if (declared_length != frame_length || frame[2] != expected_id) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *payload = frame + 3;
    *payload_length = frame_length - 3;
    if (*payload_length == 1) {
        switch ((*payload)[0]) {
        case 0x10:
            return ESP_ERR_TIMEOUT;
        case 0x11:
            return ESP_ERR_NOT_SUPPORTED;
        case 0x12:
        case 0x13:
            return ESP_ERR_INVALID_STATE;
        default:
            break;
        }
    }
    return ESP_OK;
}

esp_err_t bzm_bridge_decode_info(const uint8_t *payload,
                                 size_t payload_length,
                                 bzm_bridge_info_t *info)
{
    if (payload == NULL || info == NULL || payload_length < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t version_length = payload[3];
    if (payload[0] != BZM_BRIDGE_INFO_SCHEMA_VERSION ||
        version_length == 0 ||
        version_length > BZM_BRIDGE_VERSION_MAX_LENGTH ||
        payload_length != 4 + version_length) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (size_t i = 0; i < version_length; ++i) {
        if (payload[4 + i] < 0x20 || payload[4 + i] > 0x7e) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    memset(info, 0, sizeof(*info));
    info->schema_version = payload[0];
    info->protocol_major = payload[1];
    info->protocol_minor = payload[2];
    memcpy(info->version, payload + 4, version_length);
    return ESP_OK;
}

bool bzm_bridge_info_supports_safety(const bzm_bridge_info_t *info)
{
    return info != NULL &&
           info->schema_version == BZM_BRIDGE_INFO_SCHEMA_VERSION &&
           info->protocol_major == BZM_BRIDGE_PROTOCOL_MAJOR;
}

static void invalidate_safety_status(bzm_bridge_safety_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->state = BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED;
    status->fault = BZM_BRIDGE_SAFETY_FAULT_STATUS_INVALID;
    status->runtime_verdict = BZM_BRIDGE_SAFETY_RUNTIME_BAD_FAULT;
    status->production_verdict = BZM_BRIDGE_SAFETY_PRODUCTION_BAD_RUNTIME;
}

static uint16_t read_le16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static bool valid_safety_stage(uint8_t value)
{
    return value <= BZM_BRIDGE_SAFETY_STAGE_TRIP_LATCH;
}

static bool valid_safety_state(uint8_t value)
{
    return value <= BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED;
}

static bool valid_safety_fault(uint8_t value)
{
    return value <= BZM_BRIDGE_SAFETY_FAULT_ASIC_TRIP;
}

static bool valid_runtime_verdict(uint8_t value)
{
    return value == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF ||
           value == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED ||
           value == BZM_BRIDGE_SAFETY_RUNTIME_BAD_FAULT ||
           value == BZM_BRIDGE_SAFETY_RUNTIME_BAD_LEASE ||
           value == BZM_BRIDGE_SAFETY_RUNTIME_BAD_TRIP_INPUT ||
           value == BZM_BRIDGE_SAFETY_RUNTIME_BAD_UNSAFE_OUTPUTS;
}

static bool valid_production_verdict(uint8_t value)
{
    return value == BZM_BRIDGE_SAFETY_PRODUCTION_GOOD ||
           value == BZM_BRIDGE_SAFETY_PRODUCTION_BAD_STAGE_DISABLED ||
           value == BZM_BRIDGE_SAFETY_PRODUCTION_BAD_CAPABILITY_GAP ||
           value == BZM_BRIDGE_SAFETY_PRODUCTION_BAD_RUNTIME;
}

esp_err_t bzm_bridge_decode_safety_status(
    const uint8_t *payload, size_t payload_length,
    bzm_bridge_safety_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    invalidate_safety_status(status);
    if (payload == NULL) return ESP_ERR_INVALID_ARG;
    if (payload_length != BZM_BRIDGE_SAFETY_STATUS_LENGTH) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t stage_value = payload[1];
    uint8_t state_value = payload[2];
    uint8_t fault_value = payload[3];
    uint8_t runtime_value = payload[4];
    uint8_t production_value = payload[5];
    uint16_t capabilities = read_le16(payload + 6);
    uint16_t evidence = read_le16(payload + 8);
    uint32_t lease_remaining_ms = read_le32(payload + 10);
    uint8_t output_flags = payload[14];
    uint8_t fan_percent = payload[15];
    uint8_t trip_value = payload[16];

    const uint16_t known_capabilities =
        BZM_BRIDGE_SAFETY_CAP_5V_CONTROL |
        BZM_BRIDGE_SAFETY_CAP_ASIC_RESET_CONTROL |
        BZM_BRIDGE_SAFETY_CAP_FAN_FORCE_FULL |
        BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED |
        BZM_BRIDGE_SAFETY_CAP_CORE_POWER_CUTOFF |
        BZM_BRIDGE_SAFETY_CAP_FAN_TACH_INTERLOCK |
        BZM_BRIDGE_SAFETY_CAP_INDEPENDENT_TRIP_MONITOR |
        BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED;
    const uint16_t known_evidence =
        BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE |
        BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID |
        BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR |
        BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR |
        BZM_BRIDGE_SAFETY_EVIDENCE_CORE_CUTOFF_AVAILABLE |
        BZM_BRIDGE_SAFETY_EVIDENCE_FAN_TACH_INTERLOCK_AVAILABLE |
        BZM_BRIDGE_SAFETY_EVIDENCE_INDEPENDENT_TRIP_MONITOR_AVAILABLE;
    const uint16_t production_capabilities =
        BZM_BRIDGE_SAFETY_CAP_CORE_POWER_CUTOFF |
        BZM_BRIDGE_SAFETY_CAP_FAN_TACH_INTERLOCK |
        BZM_BRIDGE_SAFETY_CAP_INDEPENDENT_TRIP_MONITOR;

    if (payload[0] != BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION ||
        !valid_safety_stage(stage_value) ||
        !valid_safety_state(state_value) ||
        !valid_safety_fault(fault_value) ||
        !valid_runtime_verdict(runtime_value) ||
        !valid_production_verdict(production_value) ||
        (capabilities & (uint16_t)~known_capabilities) != 0 ||
        (evidence & (uint16_t)~known_evidence) != 0 ||
        (output_flags & (uint8_t)~0x07U) != 0 ||
        fan_percent > 100 || trip_value > 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bzm_bridge_safety_stage_t stage =
        (bzm_bridge_safety_stage_t)stage_value;
    bzm_bridge_safety_state_t state =
        (bzm_bridge_safety_state_t)state_value;
    bzm_bridge_safety_fault_t fault =
        (bzm_bridge_safety_fault_t)fault_value;
    bzm_bridge_safety_runtime_verdict_t runtime_verdict =
        (bzm_bridge_safety_runtime_verdict_t)runtime_value;
    bzm_bridge_safety_production_verdict_t production_verdict =
        (bzm_bridge_safety_production_verdict_t)production_value;
    bool five_volt_enabled = (output_flags & 0x01U) != 0;
    bool asic_reset_asserted = (output_flags & 0x02U) != 0;
    bool fan_full = (output_flags & 0x04U) != 0;
    bool trip_input_asserted = trip_value != 0;
    bool outputs_safe = !five_volt_enabled && asic_reset_asserted &&
                        fan_percent == 100;
    bool lease_valid = stage == BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE ||
                       (state == BZM_BRIDGE_SAFETY_STATE_CONTROLLED &&
                        lease_remaining_ms > 0);
    bool fault_clear = fault == BZM_BRIDGE_SAFETY_FAULT_NONE;

    if (fan_full != (fan_percent == 100) ||
        ((evidence & BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE) != 0) !=
            outputs_safe ||
        ((evidence & BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID) != 0) !=
            lease_valid ||
        ((evidence & BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR) != 0) ==
            trip_input_asserted ||
        ((evidence & BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR) != 0) !=
            fault_clear ||
        (evidence & production_capabilities) !=
            (capabilities & production_capabilities)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool fault_latched = state == BZM_BRIDGE_SAFETY_STATE_FAULT_LATCHED;
    if (fault_latched != !fault_clear ||
        (state == BZM_BRIDGE_SAFETY_STATE_SAFE_OFF && !outputs_safe) ||
        (fault_latched && (!outputs_safe || lease_remaining_ms != 0)) ||
        (stage == BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE &&
         (lease_remaining_ms != 0 || fault_latched)) ||
        (stage != BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE &&
         state != BZM_BRIDGE_SAFETY_STATE_CONTROLLED &&
         lease_remaining_ms != 0) ||
        (stage == BZM_BRIDGE_SAFETY_STAGE_LEASE &&
         fault == BZM_BRIDGE_SAFETY_FAULT_ASIC_TRIP) ||
        (stage == BZM_BRIDGE_SAFETY_STAGE_TRIP_LATCH &&
         trip_input_asserted && !fault_latched)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bzm_bridge_safety_runtime_verdict_t expected_runtime;
    if (!fault_clear) {
        expected_runtime = BZM_BRIDGE_SAFETY_RUNTIME_BAD_FAULT;
    } else if (trip_input_asserted) {
        expected_runtime = BZM_BRIDGE_SAFETY_RUNTIME_BAD_TRIP_INPUT;
    } else if (state == BZM_BRIDGE_SAFETY_STATE_SAFE_OFF) {
        expected_runtime = BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF;
    } else if (stage != BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE &&
               !lease_valid) {
        expected_runtime = BZM_BRIDGE_SAFETY_RUNTIME_BAD_LEASE;
    } else {
        expected_runtime = BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED;
    }
    if (runtime_verdict != expected_runtime) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bzm_bridge_safety_production_verdict_t expected_production;
    bool runtime_good =
        runtime_verdict == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF ||
        runtime_verdict == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED;
    if (stage != BZM_BRIDGE_SAFETY_STAGE_TRIP_LATCH) {
        expected_production =
            BZM_BRIDGE_SAFETY_PRODUCTION_BAD_STAGE_DISABLED;
    } else if (!runtime_good) {
        expected_production = BZM_BRIDGE_SAFETY_PRODUCTION_BAD_RUNTIME;
    } else if (capabilities != known_capabilities) {
        expected_production =
            BZM_BRIDGE_SAFETY_PRODUCTION_BAD_CAPABILITY_GAP;
    } else {
        expected_production = BZM_BRIDGE_SAFETY_PRODUCTION_GOOD;
    }
    if (production_verdict != expected_production) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *status = (bzm_bridge_safety_status_t) {
        .valid = true,
        .schema_version = payload[0],
        .stage = stage,
        .state = state,
        .fault = fault,
        .runtime_verdict = runtime_verdict,
        .production_verdict = production_verdict,
        .capabilities = capabilities,
        .evidence = evidence,
        .lease_remaining_ms = lease_remaining_ms,
        .five_volt_enabled = five_volt_enabled,
        .asic_reset_asserted = asic_reset_asserted,
        .fan_full = fan_full,
        .fan_percent = fan_percent,
        .trip_input_asserted = trip_input_asserted,
    };
    return ESP_OK;
}

static esp_err_t read_exact(uint8_t *buffer, size_t length,
                            uint32_t timeout_ms)
{
    size_t offset = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (offset < length) {
        int64_t remaining_us = deadline - esp_timer_get_time();
        if (remaining_us <= 0) return ESP_ERR_TIMEOUT;
        TickType_t ticks = pdMS_TO_TICKS((remaining_us + 999) / 1000);
        if (ticks == 0) ticks = 1;
        int received = uart_read_bytes(BZM_BRIDGE_UART, buffer + offset,
                                       length - offset, ticks);
        if (received < 0) return ESP_FAIL;
        offset += received;
    }
    return ESP_OK;
}

static esp_err_t transact(uint8_t page, uint8_t command,
                          const uint8_t *request_payload,
                          size_t request_payload_length,
                          uint8_t *response_payload,
                          size_t response_capacity,
                          size_t *response_length, uint32_t timeout_ms)
{
    uint8_t request[BZM_BRIDGE_MAX_REQUEST_SIZE];
    uint8_t response[BZM_BRIDGE_MAX_RESPONSE_SIZE];
    uint8_t command_id;
    size_t request_length;
    esp_err_t err = ESP_OK;

    pthread_mutex_lock(&BRIDGE_LOCK);
    if (!INITIALIZED) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    command_id = NEXT_COMMAND_ID++;
    request_length = bzm_bridge_encode_request(
        command_id, page, command, request_payload, request_payload_length,
        request, sizeof(request));
    if (request_length == 0) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    uart_flush_input(BZM_BRIDGE_UART);
    if (uart_write_bytes(BZM_BRIDGE_UART, request, request_length) !=
        (int)request_length) {
        err = ESP_FAIL;
        goto done;
    }
    err = uart_wait_tx_done(BZM_BRIDGE_UART, pdMS_TO_TICKS(100));
    if (err != ESP_OK) goto done;

    err = read_exact(response, 3, timeout_ms);
    if (err != ESP_OK) goto done;
    size_t frame_length = (size_t)response[0] | ((size_t)response[1] << 8);
    if (frame_length < 3 || frame_length > sizeof(response)) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    err = read_exact(response + 3, frame_length - 3, timeout_ms);
    if (err != ESP_OK) goto done;

    const uint8_t *decoded_payload;
    size_t decoded_length;
    err = bzm_bridge_decode_response(command_id, response, frame_length,
                                     &decoded_payload, &decoded_length);
    if (err != ESP_OK) goto done;
    if (decoded_length > response_capacity ||
        (decoded_length != 0 && response_payload == NULL)) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    if (decoded_length != 0) {
        memcpy(response_payload, decoded_payload, decoded_length);
    }
    if (response_length != NULL) *response_length = decoded_length;

done:
    pthread_mutex_unlock(&BRIDGE_LOCK);
    return err;
}

static esp_err_t set_gpio(uint8_t command, bool high)
{
    uint8_t request = high ? 1 : 0;
    uint8_t response;
    size_t response_length = 0;
    ESP_RETURN_ON_ERROR(transact(BZM_BRIDGE_PAGE_GPIO, command, &request, 1,
                                 &response, 1, &response_length, 200),
                        TAG, "bridge GPIO command failed");
    return response_length == 1 && response == request
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t BZM_bridge_init(void)
{
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    ESP_LOGE(TAG, "Bonanza bridge GPIO43/44 conflict with the UART console; build with configs/sdkconfig.bonanza");
    return ESP_ERR_INVALID_STATE;
#endif
    pthread_mutex_lock(&BRIDGE_LOCK);
    if (INITIALIZED) {
        pthread_mutex_unlock(&BRIDGE_LOCK);
        return ESP_OK;
    }

    uart_config_t config = {
        .baud_rate = BZM_BRIDGE_CONTROL_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    esp_err_t err = uart_param_config(BZM_BRIDGE_UART, &config);
    if (err == ESP_OK) {
        err = uart_set_pin(BZM_BRIDGE_UART,
                           BZM_BRIDGE_CONTROL_TX_GPIO,
                           BZM_BRIDGE_CONTROL_RX_GPIO,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_driver_install(
            BZM_BRIDGE_UART, BZM_BRIDGE_UART_BUFFER_SIZE,
            BZM_BRIDGE_UART_BUFFER_SIZE, 0, NULL, 0);
    }
    if (err != ESP_OK) {
        pthread_mutex_unlock(&BRIDGE_LOCK);
        ESP_LOGE(TAG, "control UART setup failed: %s", esp_err_to_name(err));
        return err;
    }

    INITIALIZED = true;
    pthread_mutex_unlock(&BRIDGE_LOCK);

    err = BZM_bridge_set_5v_enabled(false);
    if (err == ESP_OK) err = BZM_bridge_set_asic_reset(false);
    if (err != ESP_OK) {
        pthread_mutex_lock(&BRIDGE_LOCK);
        INITIALIZED = false;
        uart_driver_delete(BZM_BRIDGE_UART);
        pthread_mutex_unlock(&BRIDGE_LOCK);
        return err;
    }
    ESP_LOGI(TAG, "Bonanza bridge ready on TX%d/RX%d",
             BZM_BRIDGE_CONTROL_TX_GPIO, BZM_BRIDGE_CONTROL_RX_GPIO);
    return ESP_OK;
}

esp_err_t BZM_bridge_get_info(bzm_bridge_info_t *info)
{
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t response[BZM_BRIDGE_VERSION_MAX_LENGTH + 4];
    size_t response_length = 0;
    ESP_RETURN_ON_ERROR(
        transact(BZM_BRIDGE_PAGE_SYSTEM, BZM_BRIDGE_SYSTEM_GET_INFO,
                 NULL, 0, response, sizeof(response),
                 &response_length, 500),
        TAG, "bridge info command failed");
    return bzm_bridge_decode_info(response, response_length, info);
}

static esp_err_t safety_command(uint8_t command,
                                bzm_bridge_safety_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    invalidate_safety_status(status);

    uint8_t response[BZM_BRIDGE_SAFETY_STATUS_LENGTH];
    size_t response_length = 0;
    esp_err_t err = transact(BZM_BRIDGE_PAGE_SYSTEM, command, NULL, 0,
                             response, sizeof(response), &response_length,
                             500);
    if (err == ESP_ERR_INVALID_SIZE) return ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    return bzm_bridge_decode_safety_status(response, response_length, status);
}

esp_err_t BZM_bridge_get_safety_status(bzm_bridge_safety_status_t *status)
{
    return safety_command(BZM_BRIDGE_SYSTEM_GET_SAFETY_STATUS, status);
}

bool bzm_bridge_safety_status_allows_fault_clear(
    const bzm_bridge_safety_status_t *status)
{
    return status != NULL && status->valid &&
           !status->five_volt_enabled && status->asic_reset_asserted &&
           status->fan_full && status->fan_percent == 100 &&
           !status->trip_input_asserted;
}

esp_err_t BZM_bridge_arm_safety(bzm_bridge_safety_status_t *status)
{
    return safety_command(BZM_BRIDGE_SYSTEM_ARM_SAFETY_LEASE, status);
}

esp_err_t BZM_bridge_safety_heartbeat(bzm_bridge_safety_status_t *status)
{
    return safety_command(BZM_BRIDGE_SYSTEM_SAFETY_HEARTBEAT, status);
}

esp_err_t BZM_bridge_clear_safety_fault(
    bzm_bridge_safety_status_t *status)
{
    return safety_command(BZM_BRIDGE_SYSTEM_CLEAR_SAFETY_FAULT, status);
}

esp_err_t BZM_bridge_disarm_safety(bzm_bridge_safety_status_t *status)
{
    return safety_command(BZM_BRIDGE_SYSTEM_DISARM_SAFETY_LEASE, status);
}

esp_err_t BZM_bridge_set_5v_enabled(bool enabled)
{
    return set_gpio(BZM_BRIDGE_GPIO_5V_ENABLE, enabled);
}

esp_err_t BZM_bridge_set_asic_reset(bool high)
{
    return set_gpio(BZM_BRIDGE_GPIO_ASIC_RESET, high);
}

esp_err_t BZM_bridge_pulse_asic_reset(void)
{
    ESP_RETURN_ON_ERROR(BZM_bridge_set_asic_reset(false), TAG,
                        "ASIC reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(BZM_bridge_set_asic_reset(true), TAG,
                        "ASIC reset high failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t BZM_bridge_set_fan_percent(float percent)
{
    if (!isfinite(percent) || percent < 0.0f || percent > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t request = (uint8_t)lroundf(percent * 100.0f);
    uint8_t response;
    size_t response_length = 0;
    ESP_RETURN_ON_ERROR(transact(BZM_BRIDGE_PAGE_FAN,
                                 BZM_BRIDGE_FAN_SET_SPEED, &request, 1,
                                 &response, 1, &response_length, 200),
                        TAG, "fan command failed");
    return response_length == 1 && response == 0
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t BZM_bridge_get_fan_rpm(uint16_t *rpm)
{
    if (rpm == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t response[2];
    size_t response_length = 0;
    ESP_RETURN_ON_ERROR(transact(BZM_BRIDGE_PAGE_FAN,
                                 BZM_BRIDGE_FAN_GET_TACH, NULL, 0,
                                 response, sizeof(response),
                                 &response_length, 1000),
                        TAG, "tach command failed");
    if (response_length != 2) return ESP_ERR_INVALID_RESPONSE;
    *rpm = (uint16_t)response[0] | ((uint16_t)response[1] << 8);
    return ESP_OK;
}
