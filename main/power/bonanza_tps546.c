#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pmbus_commands.h"
#define PMBUS_STATUS_VOUT_SELECTOR 0x7A
#define PMBUS_STATUS_IOUT_SELECTOR 0x7B
#define PMBUS_STATUS_INPUT_SELECTOR 0x7C
#define PMBUS_STATUS_TEMPERATURE_SELECTOR 0x7D
#define PMBUS_STATUS_CML_SELECTOR 0x7E
#define PMBUS_STATUS_OTHER_SELECTOR 0x7F
#define PMBUS_STATUS_MFR_SPECIFIC_SELECTOR 0x80

#include "i2c_bitaxe.h"
#include "global_state.h"
#include "bonanza_tps546.h"
#include "bzm/power.h"

//#define DEBUG_TPS546_MEAS 1 //uncomment to debug TPS546 measurements
//#define DEBUG_TPS546_STATUS 1 //uncomment to debug TPS546 status bits

#define I2C_MASTER_NUM 0 /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */

#define WRITE_BIT      I2C_MASTER_WRITE
#define READ_BIT       I2C_MASTER_READ
#define ACK_CHECK      true
#define NO_ACK_CHECK   false
#define ACK_VALUE      0x0
#define NACK_VALUE     0x1
#define MAX_BLOCK_LEN  32
#define BONANZA_TPS546_POWER_GOOD_GRACE_MS 250
#define BONANZA_TPS546_I2C_TIMEOUT_MS 500

static const char *TAG = "TPS546";
static void BONANZA_TPS546_read_mfr_info(uint8_t *read_mfr_revision);
static esp_err_t BONANZA_TPS546_clear_faults(void);

static uint8_t DEVICE_ID_TPS546D24A[] = {0x54, 0x49, 0x54, 0x6D, 0x24, 0x41};
static uint8_t DEVICE_ID_TPS546D24S[] = {0x54, 0x49, 0x54, 0x6D, 0x24, 0x62};
// static uint8_t DEVICE_ID_TPS546B24A[] = {0x54, 0x49, 0x54, 0x6B, 0x24, 0x41};
// static uint8_t DEVICE_ID_TPS546B24S[] = {0x54, 0x49, 0x54, 0x6B, 0x24, 0x62};

static i2c_master_dev_handle_t tps546_i2c_handle;

static const bzm_tps546_profile_t *const tps546_config = &BZM_TPS546_BIRDS_PROFILE;
static uint8_t tps546_vout_mode;
static bool tps546_config_written;
static TickType_t tps546_power_good_grace_until = 0;
static i2c_master_dev_handle_t tps546_alert_i2c_handle;

// Cached values to handle I2C failures robustly
static float last_vin = 0.0f;
static float last_iout = 0.0f;
static float last_vout = 0.0f;
static int last_temp = 0;

static esp_err_t BONANZA_TPS546_parse_status(uint16_t);

static esp_err_t BONANZA_TPS546_read_alert_response(uint8_t *alert_response)
{
    ESP_RETURN_ON_ERROR(i2c_bitaxe_add_device(BONANZA_TPS546_I2CADDR_ALERT, &tps546_alert_i2c_handle, "BONANZA_TPS546_ALERT"),
                        TAG, "Failed to add TPS546 SMBus alert address");

    return i2c_master_receive(tps546_alert_i2c_handle, alert_response, 1, BONANZA_TPS546_I2C_TIMEOUT_MS);
}

/**
 * @brief SMBus read byte
 * @param command The command to read
 * @param data Pointer to store the read data
 */
static esp_err_t smb_read_byte(uint8_t command, uint8_t *data)
{
    return i2c_bitaxe_register_read(tps546_i2c_handle, command, data, 1);
}

/**
 * @brief SMBus write byte
 * @param command The command to write
 * @param data The data to write
 */
static esp_err_t smb_write_byte(uint8_t command, uint8_t data)
{
    return i2c_bitaxe_register_write_byte(tps546_i2c_handle, command, data);
}

/**
 * @brief SMBus write addr
 * @param command The command to write
 */
static esp_err_t smb_write_addr(uint8_t command)
{
    return i2c_bitaxe_register_write_addr(tps546_i2c_handle, command);
}

/**
 * @brief SMBus read word
 * @param command The command to read
 * @param result Pointer to store the read data
 */
static esp_err_t smb_read_word(uint8_t command, uint16_t *result)
{
    uint8_t data[2];
    if (i2c_bitaxe_register_read(tps546_i2c_handle, command, data, 2) != ESP_OK) {
        return ESP_FAIL;
    } else {
        *result = (data[1] << 8) + data[0];
        return ESP_OK;
    }
}

/**
 * @brief SMBus write word
 * @param command The command to write
 * @param data The data to write
 */
static esp_err_t smb_write_word(uint8_t command, uint16_t data)
{
    return i2c_bitaxe_register_write_word(tps546_i2c_handle, command, data);
}

/**
 * @brief SMBus read block -- SMBus is funny in that the first byte returned is the length of data??
 * @param command The command to read
 * @param data Pointer to store the read data
 * @param len The number of bytes to read
 */
static esp_err_t smb_read_block(uint8_t command, uint8_t *data, uint8_t len)
{
    //malloc a buffer len+1 to store the length byte
    uint8_t *buf = (uint8_t *)malloc(len+1);
    if (buf == NULL) return ESP_ERR_NO_MEM;
    if (i2c_bitaxe_register_read(tps546_i2c_handle, command, buf, len+1) != ESP_OK) {
        free(buf);
        return ESP_FAIL;
    }
    //copy the data into the buffer
    memcpy(data, buf+1, len);
    free(buf);

    return ESP_OK;
}

/**
 * @brief Strict SMBus block read used for safety-critical profile readback.
 *
 * Unlike the legacy helper above, this validates the device-supplied block
 * length instead of silently copying a short or differently shaped response.
 */
static esp_err_t smb_read_block_exact(uint8_t command, uint8_t *data,
                                      uint8_t len)
{
    if (data == NULL || len == 0 || len > MAX_BLOCK_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[MAX_BLOCK_LEN + 1] = {0};
    esp_err_t err = i2c_bitaxe_register_read(tps546_i2c_handle, command,
                                             buf, len + 1);
    if (err != ESP_OK) return err;
    if (buf[0] != len) return ESP_ERR_INVALID_SIZE;

    memcpy(data, &buf[1], len);
    return ESP_OK;
}

/**
 * @brief SMBus write block - don;t forget the length byte first :P
 * @param command The command to write
 * @param data The data to write
 * @param len The number of bytes to write
 */
static esp_err_t smb_write_block(uint8_t command, const uint8_t *data, uint8_t len)
{
    //malloc a buffer len+2 to store the command byte and then the length byte
    uint8_t *buf = (uint8_t *)malloc(len+2);
    if (buf == NULL) return ESP_ERR_NO_MEM;
    buf[0] = command;
    buf[1] = len;
    //copy the data into the buffer
    memcpy(buf+2, data, len);

    //write it all
    if (i2c_bitaxe_register_write_bytes(tps546_i2c_handle, buf, len+2) != ESP_OK) {
        free(buf);
        return ESP_FAIL;
    } else {
        free(buf);
        return ESP_OK;
    }
}

/**
 * @brief Convert an SLINEAR11 value into an int
 * @param value The SLINEAR11 value to convert
 */
static int slinear11_2_int(uint16_t value)
{
    int exponent, mantissa;
    float result;

    // First 5 bits is exponent in twos-complement
    // check the first bit of the exponent to see if its negative
    if (value & 0x8000) {
        // exponent is negative
        exponent = -1 * (((~value >> 11) & 0x001F) + 1);
    } else {
        exponent = (value >> 11);
    }
    // last 11 bits is the mantissa in twos-complement
    // check the first bit of the mantissa to see if its negative
    if (value & 0x400) {
        // mantissa is negative
        mantissa = -1 * ((~value & 0x03FF) + 1);
    } else {
        mantissa = (value & 0x03FF);
    }

    // calculate result (mantissa * 2^exponent)
    result = mantissa * powf(2.0, exponent);
    return (int)result;
}

/**
 * @brief Convert an SLINEAR11 value into an int
 * @param value The SLINEAR11 value to convert
 */
static float slinear11_2_float(uint16_t value)
{
    int exponent, mantissa;
    float result;

    // First 5 bits is exponent in twos-complement
    // check the first bit of the exponent to see if its negative
    if (value & 0x8000) {
        // exponent is negative
        exponent = -1 * (((~value >> 11) & 0x001F) + 1);
    } else {
        exponent = (value >> 11);
    }
    // last 11 bits is the mantissa in twos-complement
    // check the first bit of the mantissa to see if its negative
    if (value & 0x400) {
        // mantissa is negative
        mantissa = -1 * ((~value & 0x03FF) + 1);
    } else {
        mantissa = (value & 0x03FF);
    }

    // calculate result (mantissa * 2^exponent)
    result = mantissa * powf(2.0, exponent);
    return result;
}

/**
 * @brief Convert an int value into an SLINEAR11
 * @param value The int value to convert
 */
static uint16_t int_2_slinear11(int value)
{
    int mantissa;
    int exponent = 0;
    uint16_t result = 0;
    int i;

    // First see if the exponent is positive or negative
    if (value >= 0) {
        // exponent is positive
        for (i=0; i<=15; i++) {
            mantissa = value / powf(2.0, i);
            if (mantissa < 1024) {
                exponent = i;
                break;
            }
        }
        if (i == 16) {
            ESP_LOGI(TAG, "Could not find a solution");
            return 0;
        }
    } else {
        // value is negative
        ESP_LOGI(TAG, "No negative numbers at this time");
        return 0;
    }

    result = ((exponent << 11) & 0xF800) + mantissa;

    return result;
}

/**
 * @brief Convert a float value into an SLINEAR11
 * @param value The float value to convert
 */
static uint16_t float_2_slinear11(float value)
{
    int mantissa;
    int exponent = 0;
    uint16_t result = 0;
    int i;

    // First see if the exponent is positive or negative
    if (value > 0) {
        // exponent is negative
        for (i=0; i<=15; i++) {
            mantissa = value * powf(2.0, i);
            if (mantissa >= 1024) {
                exponent = i-1;
                mantissa = value * powf(2.0, exponent);
                break;
            }
        }
        if (i == 16) {
            ESP_LOGI(TAG, "Could not find a solution");
            return 0;
        }
    } else {
        // value is negative
        ESP_LOGI(TAG, "No negative numbers at this time");
        return 0;
    }

    result = (( (~exponent + 1) << 11) & 0xF800) + mantissa;

    return result;
}

/**
 * @brief Convert a ULINEAR16 value into a float
 * the exponent comes from the VOUT_MODE bits[4..0]
 * stored in twos-complement
 * The mantissa occupies the full 16-bits of the value
 * @param value The ULINEAR16 value to convert
 */
static float ulinear16_2_float_mode(uint16_t value, uint8_t mode)
{
    int exponent = mode & 0x1f;
    if (exponent & 0x10) exponent -= 32;
    return value * powf(2.0f, exponent);
}

static float ulinear16_2_float(uint16_t value)
{
    uint8_t mode;
    if (smb_read_byte(PMBUS_VOUT_MODE, &mode) != ESP_OK) return NAN;
    return ulinear16_2_float_mode(value, mode);
}

/**
 * @brief Convert a float value into a ULINEAR16
 * the exponent comes from the VOUT_MODE bits[4..0]
 * stored in twos-complement
 * The mantissa occupies the full 16-bits of the result
 * @param value The float value to convert
*/
static uint16_t float_2_ulinear16(float value)
{
    uint8_t voutmode;
    float exponent;
    uint16_t result;

    smb_read_byte(PMBUS_VOUT_MODE, &voutmode);
    if (voutmode & 0x10) {
        // exponent is negative
        exponent = -1 * ((~voutmode & 0x1F) + 1);
    } else {
        exponent = (voutmode & 0x1F);
    }

    result = (value / powf(2.0, exponent));

    return result;
}

static uint16_t float_2_ulinear16_mode(float value, uint8_t voutmode)
{
    int exponent;
    if (voutmode & 0x10) {
        exponent = -1 * ((~voutmode & 0x1F) + 1);
    } else {
        exponent = (voutmode & 0x1F);
    }
    return (uint16_t)(value / powf(2.0f, exponent));
}

static esp_err_t write_config(void)
{
    static const uint8_t status_selectors[] = {
        PMBUS_STATUS_VOUT_SELECTOR,
        PMBUS_STATUS_IOUT_SELECTOR,
        PMBUS_STATUS_INPUT_SELECTOR,
        PMBUS_STATUS_TEMPERATURE_SELECTOR,
        PMBUS_STATUS_CML_SELECTOR,
        PMBUS_STATUS_OTHER_SELECTOR,
        PMBUS_STATUS_MFR_SPECIFIC_SELECTOR,
    };

    uint8_t vout_mode = 0;
    tps546_config_written = false;
    ESP_RETURN_ON_ERROR(smb_read_byte(PMBUS_VOUT_MODE, &vout_mode),
                        TAG, "read VOUT_MODE for Bonanza profile failed");

    uint8_t on_off_config = ON_OFF_CONFIG_DELAY | ON_OFF_CONFIG_POLARITY |
                            ON_OFF_CONFIG_CMD | ON_OFF_CONFIG_PU;
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_ON_OFF_CONFIG, on_off_config),
                        TAG, "write ON_OFF_CONFIG failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_PHASE,
                                       tps546_config->phase),
                        TAG, "write PHASE failed");
    for (size_t i = 0; i < sizeof(status_selectors); ++i) {
        uint16_t value = tps546_config->smbalert_mask[i] |
                         status_selectors[i];
        ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_SMBALERT_MASK, value),
                            TAG, "write SMBALERT_MASK failed");
    }
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_FREQUENCY_SWITCH,
                            int_2_slinear11(tps546_config->frequency_switch_khz)),
                        TAG, "write FREQUENCY_SWITCH failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(
                            PMBUS_SYNC_CONFIG,
                            tps546_config->sync_config),
                        TAG, "write SYNC_CONFIG failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_STACK_CONFIG,
                            tps546_config->stack_config),
                        TAG, "write STACK_CONFIG failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_INTERLEAVE,
                            tps546_config->interleave),
                        TAG, "write INTERLEAVE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_MISC_OPTIONS,
                            tps546_config->misc_options),
                        TAG, "write MISC_OPTIONS failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_PIN_DETECT_OVERRIDE,
                            tps546_config->pin_detect_override),
                        TAG, "write PIN_DETECT_OVERRIDE failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_SLAVE_ADDRESS,
                                       BONANZA_TPS546_I2CADDR),
                        TAG, "write SLAVE_ADDRESS failed");
    ESP_RETURN_ON_ERROR(smb_write_block(
                            PMBUS_COMPENSATION_CONFIG,
                            tps546_config->compensation_config, 5),
                        TAG, "write COMPENSATION_CONFIG failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(smb_write_block(
                            PMBUS_POWER_STAGE_CONFIG,
                            &tps546_config->power_stage_config, 1),
                        TAG, "write POWER_STAGE_CONFIG failed");
    ESP_RETURN_ON_ERROR(smb_write_block(
                            PMBUS_TELEMETRY_CFG,
                            tps546_config->telemetry_config, 6),
                        TAG, "write TELEMETRY_CONFIG failed");

    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_COMMAND,
                            float_2_ulinear16_mode(
                                tps546_config->vout_command,
                                vout_mode)),
                        TAG, "write VOUT_COMMAND failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_TRIM,
                            tps546_config->vout_trim),
                        TAG, "write VOUT_TRIM failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_MAX,
                            float_2_ulinear16_mode(
                                tps546_config->vout_max,
                                vout_mode)),
                        TAG, "write VOUT_MAX failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_MARGIN_HIGH,
                            float_2_ulinear16_mode(
                                tps546_config->vout_margin_high,
                                vout_mode)),
                        TAG, "write VOUT_MARGIN_HIGH failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_MARGIN_LOW,
                            float_2_ulinear16_mode(
                                tps546_config->vout_margin_low,
                                vout_mode)),
                        TAG, "write VOUT_MARGIN_LOW failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_TRANSITION_RATE,
                            tps546_config->vout_transition_rate),
                        TAG, "write VOUT_TRANSITION_RATE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_SCALE_LOOP,
                            float_2_slinear11(tps546_config->vout_scale_loop)),
                        TAG, "write VOUT_SCALE_LOOP failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_MIN,
                            float_2_ulinear16_mode(
                                tps546_config->vout_min,
                                vout_mode)),
                        TAG, "write VOUT_MIN failed");

    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VIN_ON,
                            float_2_slinear11(tps546_config->vin_on)),
                        TAG, "write VIN_ON failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VIN_OFF,
                            float_2_slinear11(tps546_config->vin_off)),
                        TAG, "write VIN_OFF failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_IOUT_CAL_GAIN,
                            tps546_config->iout_cal_gain),
                        TAG, "write IOUT_CAL_GAIN failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_IOUT_CAL_OFFSET,
                            tps546_config->iout_cal_offset),
                        TAG, "write IOUT_CAL_OFFSET failed");

    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_OV_FAULT_LIMIT,
                            float_2_ulinear16_mode(
                                tps546_config->vout_ov_fault_limit,
                                vout_mode)),
                        TAG, "write VOUT_OV_FAULT_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_VOUT_OV_FAULT_RESPONSE,
                                       tps546_config->vout_ov_fault_response),
                        TAG, "write VOUT_OV_FAULT_RESPONSE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_OV_WARN_LIMIT,
                            float_2_ulinear16_mode(
                                tps546_config->vout_ov_warn_limit,
                                vout_mode)),
                        TAG, "write VOUT_OV_WARN_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_UV_WARN_LIMIT,
                            float_2_ulinear16_mode(
                                tps546_config->vout_uv_warn_limit,
                                vout_mode)),
                        TAG, "write VOUT_UV_WARN_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VOUT_UV_FAULT_LIMIT,
                            float_2_ulinear16_mode(
                                tps546_config->vout_uv_fault_limit,
                                vout_mode)),
                        TAG, "write VOUT_UV_FAULT_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_VOUT_UV_FAULT_RESPONSE,
                                       tps546_config->vout_uv_fault_response),
                        TAG, "write VOUT_UV_FAULT_RESPONSE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_IOUT_OC_FAULT_LIMIT,
                            float_2_slinear11(tps546_config->iout_oc_fault_limit)),
                        TAG, "write IOUT_OC_FAULT_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_IOUT_OC_FAULT_RESPONSE,
                                       tps546_config->iout_oc_fault_response),
                        TAG, "write IOUT_OC_FAULT_RESPONSE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_IOUT_OC_WARN_LIMIT,
                            float_2_slinear11(tps546_config->iout_oc_warn_limit)),
                        TAG, "write IOUT_OC_WARN_LIMIT failed");

    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_OT_FAULT_LIMIT,
                                       int_2_slinear11(tps546_config->ot_fault_limit)),
                        TAG, "write OT_FAULT_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_OT_FAULT_RESPONSE,
                                       tps546_config->ot_fault_response),
                        TAG, "write OT_FAULT_RESPONSE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_OT_WARN_LIMIT,
                                       int_2_slinear11(tps546_config->ot_warn_limit)),
                        TAG, "write OT_WARN_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VIN_OV_FAULT_LIMIT,
                            float_2_slinear11(tps546_config->vin_ov_fault_limit)),
                        TAG, "write VIN_OV_FAULT_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_VIN_OV_FAULT_RESPONSE,
                                       tps546_config->vin_ov_fault_response),
                        TAG, "write VIN_OV_FAULT_RESPONSE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(
                            PMBUS_VIN_UV_WARN_LIMIT,
                            float_2_slinear11(tps546_config->vin_uv_warn_limit)),
                        TAG, "write VIN_UV_WARN_LIMIT failed");

    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_TON_DELAY,
                                       int_2_slinear11(tps546_config->ton_delay)),
                        TAG, "write TON_DELAY failed");
    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_TON_RISE,
                                       int_2_slinear11(tps546_config->ton_rise)),
                        TAG, "write TON_RISE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_TON_MAX_FAULT_LIMIT,
                                       int_2_slinear11(tps546_config->ton_max_fault_limit)),
                        TAG, "write TON_MAX_FAULT_LIMIT failed");
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_TON_MAX_FAULT_RESPONSE,
                                       tps546_config->ton_max_fault_response),
                        TAG, "write TON_MAX_FAULT_RESPONSE failed");
    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_TOFF_DELAY,
                                       int_2_slinear11(tps546_config->toff_delay)),
                        TAG, "write TOFF_DELAY failed");
    ESP_RETURN_ON_ERROR(smb_write_word(PMBUS_TOFF_FALL,
                                       int_2_slinear11(tps546_config->toff_fall)),
                        TAG, "write TOFF_FALL failed");
    tps546_vout_mode = vout_mode;
    tps546_config_written = true;
    return ESP_OK;
}

static esp_err_t verify_config_field(const char *name,
                                     const uint8_t *expected, size_t expected_len,
                                     const uint8_t *observed, size_t observed_len,
                                     char *detail, size_t detail_len)
{
    if (expected_len != observed_len || memcmp(expected, observed, expected_len) != 0) {
        snprintf(detail, detail_len, "%s readback mismatch", name);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t config_read_failed(const char *name, const char *operation,
                                    esp_err_t err, char *detail,
                                    size_t detail_len)
{
    snprintf(detail, detail_len, "%s %s failed", name, operation);
    return err == ESP_OK ? ESP_FAIL : err;
}

static esp_err_t verify_config_byte(uint8_t command, const char *name,
                                    uint8_t expected, char *detail,
                                    size_t detail_len)
{
    uint8_t observed = 0;
    esp_err_t err = smb_read_byte(command, &observed);
    if (err != ESP_OK) {
        return config_read_failed(name, "read", err, detail, detail_len);
    }
    return verify_config_field(name, &expected, 1, &observed, 1,
                               detail, detail_len);
}

static esp_err_t verify_config_word(uint8_t command, const char *name,
                                    uint16_t expected, char *detail,
                                    size_t detail_len)
{
    uint16_t observed = 0;
    esp_err_t err = smb_read_word(command, &observed);
    if (err != ESP_OK) {
        return config_read_failed(name, "read", err, detail, detail_len);
    }

    const uint8_t expected_bytes[] = {
        (uint8_t)(expected & 0xff), (uint8_t)(expected >> 8),
    };
    const uint8_t observed_bytes[] = {
        (uint8_t)(observed & 0xff), (uint8_t)(observed >> 8),
    };
    return verify_config_field(name, expected_bytes, sizeof(expected_bytes),
                               observed_bytes, sizeof(observed_bytes),
                               detail, detail_len);
}

static esp_err_t verify_config_block(uint8_t command, const char *name,
                                     const uint8_t *expected, uint8_t len,
                                     char *detail, size_t detail_len)
{
    uint8_t observed[MAX_BLOCK_LEN] = {0};
    esp_err_t err = smb_read_block_exact(command, observed, len);
    if (err != ESP_OK) {
        return config_read_failed(name, "read", err, detail, detail_len);
    }
    return verify_config_field(name, expected, len, observed, len,
                               detail, detail_len);
}

/*--- Public TPS546 functions ---*/

/**
 * @brief Set up the TPS546 regulator and turn it on
*/
esp_err_t BONANZA_TPS546_init(void)
{
    uint8_t u8_value = 0;
    uint16_t u16_value = 0;
    uint8_t read_mfr_revision[4];
    int temp;
    uint8_t comp_config[5];
    uint8_t voutmode;

    tps546_config_written = false;

    ESP_LOGI(TAG, "Initializing the core voltage regulator");

    uint8_t alert_response = 0;
    esp_err_t alert_err = BONANZA_TPS546_read_alert_response(&alert_response);
    if (alert_err == ESP_OK) {
        ESP_LOGW(TAG, "SMBus alert response raw=0x%02X decoded 7-bit address=0x%02X", alert_response, alert_response >> 1);
    } else {
        ESP_LOGI(TAG, "No SMBus alert response read: %s", esp_err_to_name(alert_err));
    }

    ESP_RETURN_ON_ERROR(i2c_bitaxe_add_device(BONANZA_TPS546_I2CADDR, &tps546_i2c_handle, TAG), TAG, "Failed to add TPS546 I2C");

    // 1) Power-up guard (PMBus ready after AVIN UVLO + ~8 ms)
    vTaskDelay(pdMS_TO_TICKS(15));  // conservative

    // 2) Robust ID read with retries
    uint8_t id[6] = {0};
    const int max_attempts = 6;
    bool id_matched = false;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        esp_err_t err = smb_read_block(PMBUS_IC_DEVICE_ID, id, 6);  // ensure this API consumes the length byte internally
        if (err == ESP_OK) {
            if (memcmp(id, DEVICE_ID_TPS546D24A, 6) == 0
             || memcmp(id, DEVICE_ID_TPS546D24S, 6) == 0
            //  || memcmp(id, DEVICE_ID_TPS546B24A, 6) == 0
            //  || memcmp(id, DEVICE_ID_TPS546B24S, 6) == 0
                ) {
                id_matched = true;  // got a real response
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3));  // short backoff; total extra ~15 ms worst case
    }

    ESP_LOGI(TAG, "Device ID: %02x %02x %02x %02x %02x %02x", id[0], id[1], id[2], id[3], id[4], id[5]);

    if (!id_matched) {

        ESP_LOGE(TAG, "Cannot find TPS546 regulator - Device ID mismatch");
        return ESP_FAIL;
    }

    //write operation register to turn off power
    u8_value = OPERATION_OFF;
    ESP_LOGI(TAG, "Power config-OPERATION: %02X", u8_value);
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_OPERATION, u8_value), TAG,
                        "set OPERATION off failed");

    /* Make sure power is turned off until commanded */
    u8_value = (ON_OFF_CONFIG_DELAY | ON_OFF_CONFIG_POLARITY | ON_OFF_CONFIG_CP | ON_OFF_CONFIG_CMD | ON_OFF_CONFIG_PU);
    ESP_LOGI(TAG, "Power config-ON_OFF_CONFIG: %02X", u8_value);
    ESP_RETURN_ON_ERROR(smb_write_byte(PMBUS_ON_OFF_CONFIG, u8_value), TAG,
                        "set ON_OFF_CONFIG failed");

    ESP_RETURN_ON_ERROR(BONANZA_TPS546_clear_faults(), TAG,
                        "clear initial TPS546 faults failed");

    /* Read version number and see if it matches */
    BONANZA_TPS546_read_mfr_info(read_mfr_revision);
    // if (memcmp(read_mfr_revision, MFR_REVISION, 3) != 0) {

    // If it doesn't match, then write all the registers and set new version number
    // ESP_LOGI(TAG, "--------------------------------");
    // ESP_LOGI(TAG, "Config version mismatch, writing new config values");
    ESP_LOGI(TAG, "Writing new config values");
    ESP_RETURN_ON_ERROR(smb_read_byte(PMBUS_VOUT_MODE, &voutmode), TAG,
                        "read VOUT_MODE failed");
    ESP_LOGI(TAG, "VOUT_MODE: %02x", voutmode);
    ESP_RETURN_ON_ERROR(write_config(), TAG,
                        "TPS546 configuration failed");
    //}

    // /* Show temperature */
    // ESP_LOGI(TAG, "--------------------------------");
    // ESP_LOGI(TAG, "Temp: %d", BONANZA_TPS546_get_temperature());

    /* Show voltage settings */

    smb_read_word(PMBUS_STATUS_WORD, &u16_value);
    ESP_LOGI(TAG, "read STATUS_WORD: %04x", u16_value);

    ESP_LOGI(TAG, "-----------VOLTAGE/CURRENT---------------------");
    smb_read_word(PMBUS_READ_VIN, &u16_value);
    ESP_LOGI(TAG, "read READ_VIN: %.2fV", slinear11_2_float(u16_value));
    smb_read_word(PMBUS_READ_IOUT, &u16_value);
    ESP_LOGI(TAG, "read READ_IOUT: %.2fA", slinear11_2_float(u16_value));
    smb_read_word(PMBUS_READ_VOUT, &u16_value);
    ESP_LOGI(TAG, "read READ_VOUT: %.2fV", ulinear16_2_float(u16_value));

    ESP_LOGI(TAG, "-----------TIMING---------------------");
    smb_read_word(PMBUS_TON_DELAY, &u16_value);
    temp = slinear11_2_int(u16_value);
    ESP_LOGI(TAG, "read TON_DELAY: %dms", temp);
    smb_read_word(PMBUS_TON_RISE, &u16_value);
    temp = slinear11_2_int(u16_value);
    ESP_LOGI(TAG, "read TON_RISE: %dms", temp);
    smb_read_word(PMBUS_TON_MAX_FAULT_LIMIT, &u16_value);
    temp = slinear11_2_int(u16_value);
    ESP_LOGI(TAG, "read TON_MAX_FAULT_LIMIT: %dms", temp);
    smb_read_byte(PMBUS_TON_MAX_FAULT_RESPONSE, &u8_value);
    ESP_LOGI(TAG, "read TON_MAX_FAULT_RESPONSE: %02x", u8_value);
    smb_read_word(PMBUS_TOFF_DELAY, &u16_value);
    temp = slinear11_2_int(u16_value);
    ESP_LOGI(TAG, "read TOFF_DELAY: %dms", temp);
    smb_read_word(PMBUS_TOFF_FALL, &u16_value);
    temp = slinear11_2_int(u16_value);
    ESP_LOGI(TAG, "read TOFF_FALL: %dms", temp);
    ESP_LOGI(TAG, "---------CONFIG--------------------");
    smb_read_byte(PMBUS_PHASE, &u8_value);
    ESP_LOGI(TAG, "read PHASE: %02x", u8_value);
    smb_read_word(PMBUS_STACK_CONFIG, &u16_value);
    ESP_LOGI(TAG, "read STACK_CONFIG: %04x", u16_value);
    smb_read_byte(PMBUS_SYNC_CONFIG, &u8_value);
    ESP_LOGI(TAG, "read SYNC_CONFIG: %02x", u8_value);
    smb_read_word(PMBUS_INTERLEAVE, &u16_value);
    ESP_LOGI(TAG, "read INTERLEAVE: %04x", u16_value);
    smb_read_byte(PMBUS_CAPABILITY, &u8_value);
    ESP_LOGI(TAG, "read CAPABILITY: %02x", u8_value);
    ESP_LOGI(TAG, "---------OPERATION------------------");
    smb_read_byte(PMBUS_OPERATION, &u8_value);
    ESP_LOGI(TAG, "read OPERATION: %02x", u8_value);
    smb_read_byte(PMBUS_ON_OFF_CONFIG, &u8_value);
    ESP_LOGI(TAG, "read ON_OFF_CONFIG: %02x", u8_value);

    // Read the compensation config registers
    if (smb_read_block(PMBUS_COMPENSATION_CONFIG, comp_config, 5) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read COMPENSATION CONFIG");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "read COMPENSATION CONFIG");
    ESP_LOGI(TAG, "%02x %02x %02x %02x %02x", comp_config[0], comp_config[1],
        comp_config[2], comp_config[3], comp_config[4]);

    ESP_LOGI(TAG, "Clearing faults");
    BONANZA_TPS546_clear_faults();

    smb_read_word(PMBUS_STATUS_WORD, &u16_value);
    ESP_LOGI(TAG, "read STATUS_WORD: %04x", u16_value);

    return ESP_OK;
}

static esp_err_t BONANZA_TPS546_clear_faults(void) {

    ESP_RETURN_ON_ERROR(smb_write_addr(PMBUS_CLEAR_FAULTS), TAG, "Failed to write address");

    // acknowledge the SMBus fault to reset the SMBALERT pin
    //ESP_RETURN_ON_ERROR(smb_clear_alert(), TAG, "Failed to clear alert"); //this doesn't seem to work?

    return ESP_OK;
}

/**
 * @brief Read the manufacturer model and revision
 * @param read_mfr_revision Pointer to store the read revision
*/
static void BONANZA_TPS546_read_mfr_info(uint8_t *read_mfr_revision)
{
    uint8_t read_mfr_id[4];
    uint8_t read_mfr_model[4];

    ESP_LOGI(TAG, "Reading MFR info");
    if (smb_read_block(PMBUS_MFR_ID, read_mfr_id, 3) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MFR ID");
        return;
    }
    read_mfr_id[3] = 0x00;
    if (smb_read_block(PMBUS_MFR_MODEL, read_mfr_model, 3) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MFR MODEL");
        return;
    }
    read_mfr_model[3] = 0x00;
    if (smb_read_block(PMBUS_MFR_REVISION, read_mfr_revision, 3) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MFR REVISION");
        return;
    }

    ESP_LOGI(TAG, "MFR_ID: %02X %02X %02X", read_mfr_id[0], read_mfr_id[1], read_mfr_id[2]);
    ESP_LOGI(TAG, "MFR_MODEL: %02X %02X %02X", read_mfr_model[0], read_mfr_model[1], read_mfr_model[2]);
    ESP_LOGI(TAG, "MFR_REVISION: %02X %02X %02X", read_mfr_revision[0], read_mfr_revision[1], read_mfr_revision[2]);
}

esp_err_t BONANZA_TPS546_check_protection(char *detail, size_t detail_len)
{
    if (detail == NULL || detail_len == 0) return ESP_ERR_INVALID_ARG;
    detail[0] = '\0';
    if (!tps546_config_written) {
        snprintf(detail, detail_len, "Bonanza profile not active");
        return ESP_ERR_INVALID_STATE;
    }

#define VERIFY_BYTE(command, name, expected) do {                              \
    esp_err_t verify_err = verify_config_byte((command), (name), (expected),   \
                                               detail, detail_len);             \
    if (verify_err != ESP_OK) return verify_err;                               \
} while (0)
#define VERIFY_WORD(command, name, expected) do {                              \
    esp_err_t verify_err = verify_config_word((command), (name), (expected),   \
                                               detail, detail_len);             \
    if (verify_err != ESP_OK) return verify_err;                               \
} while (0)
#define VERIFY_BLOCK(command, name, expected, len) do {                        \
    esp_err_t verify_err = verify_config_block((command), (name), (expected),  \
                                                (len), detail, detail_len);      \
    if (verify_err != ESP_OK) return verify_err;                               \
} while (0)

    /* Read back the power topology, feedback, and hard protection settings.
     * Every configuration write is checked separately; optional alert routing,
     * margins and timing settings do not require a second startup audit. */

    /* VOUT_MODE is an immutable dependency of every ULINEAR16 encoding. */
    VERIFY_BYTE(PMBUS_VOUT_MODE, "VOUT_MODE", tps546_vout_mode);

    const uint8_t on_off_config = ON_OFF_CONFIG_DELAY | ON_OFF_CONFIG_POLARITY |
                                  ON_OFF_CONFIG_CMD | ON_OFF_CONFIG_PU;
    VERIFY_BYTE(PMBUS_ON_OFF_CONFIG, "ON_OFF_CONFIG", on_off_config);
    VERIFY_BYTE(PMBUS_PHASE, "PHASE", tps546_config->phase);

    VERIFY_WORD(PMBUS_FREQUENCY_SWITCH, "FREQUENCY_SWITCH",
                int_2_slinear11(tps546_config->frequency_switch_khz));
    VERIFY_WORD(PMBUS_STACK_CONFIG, "STACK_CONFIG",
                tps546_config->stack_config);
    VERIFY_WORD(PMBUS_PIN_DETECT_OVERRIDE, "PIN_DETECT_OVERRIDE",
                tps546_config->pin_detect_override);
    VERIFY_BLOCK(PMBUS_COMPENSATION_CONFIG, "COMPENSATION_CONFIG",
                 tps546_config->compensation_config, 5);
    VERIFY_BLOCK(PMBUS_POWER_STAGE_CONFIG, "POWER_STAGE_CONFIG",
                 &tps546_config->power_stage_config, 1);
    VERIFY_BLOCK(PMBUS_TELEMETRY_CFG, "TELEMETRY_CONFIG",
                 tps546_config->telemetry_config, 6);

    VERIFY_WORD(PMBUS_VOUT_COMMAND, "VOUT_COMMAND",
                float_2_ulinear16_mode(
                    tps546_config->vout_command,
                    tps546_vout_mode));
    VERIFY_WORD(PMBUS_VOUT_TRIM, "VOUT_TRIM",
                tps546_config->vout_trim);
    VERIFY_WORD(PMBUS_VOUT_MAX, "VOUT_MAX",
                float_2_ulinear16_mode(tps546_config->vout_max,
                                       tps546_vout_mode));
    VERIFY_WORD(PMBUS_VOUT_SCALE_LOOP, "VOUT_SCALE_LOOP",
                float_2_slinear11(tps546_config->vout_scale_loop));
    VERIFY_WORD(PMBUS_VOUT_MIN, "VOUT_MIN",
                float_2_ulinear16_mode(tps546_config->vout_min,
                                       tps546_vout_mode));

    VERIFY_WORD(PMBUS_VIN_ON, "VIN_ON",
                float_2_slinear11(tps546_config->vin_on));
    VERIFY_WORD(PMBUS_VIN_OFF, "VIN_OFF",
                float_2_slinear11(tps546_config->vin_off));
    VERIFY_WORD(PMBUS_IOUT_CAL_GAIN, "IOUT_CAL_GAIN",
                tps546_config->iout_cal_gain);
    VERIFY_WORD(PMBUS_IOUT_CAL_OFFSET, "IOUT_CAL_OFFSET",
                tps546_config->iout_cal_offset);

    VERIFY_WORD(PMBUS_VOUT_OV_FAULT_LIMIT, "VOUT_OV_FAULT_LIMIT",
                float_2_ulinear16_mode(
                    tps546_config->vout_ov_fault_limit,
                    tps546_vout_mode));
    VERIFY_BYTE(PMBUS_VOUT_OV_FAULT_RESPONSE, "VOUT_OV_FAULT_RESPONSE",
                tps546_config->vout_ov_fault_response);
    VERIFY_WORD(PMBUS_VOUT_UV_FAULT_LIMIT, "VOUT_UV_FAULT_LIMIT",
                float_2_ulinear16_mode(
                    tps546_config->vout_uv_fault_limit,
                    tps546_vout_mode));
    VERIFY_BYTE(PMBUS_VOUT_UV_FAULT_RESPONSE, "VOUT_UV_FAULT_RESPONSE",
                tps546_config->vout_uv_fault_response);
    VERIFY_WORD(PMBUS_IOUT_OC_FAULT_LIMIT, "IOUT_OC_FAULT_LIMIT",
                float_2_slinear11(
                    tps546_config->iout_oc_fault_limit));
    VERIFY_BYTE(PMBUS_IOUT_OC_FAULT_RESPONSE, "IOUT_OC_FAULT_RESPONSE",
                tps546_config->iout_oc_fault_response);

    VERIFY_WORD(PMBUS_OT_FAULT_LIMIT, "OT_FAULT_LIMIT",
                int_2_slinear11(tps546_config->ot_fault_limit));
    VERIFY_BYTE(PMBUS_OT_FAULT_RESPONSE, "OT_FAULT_RESPONSE",
                tps546_config->ot_fault_response);
    VERIFY_WORD(PMBUS_VIN_OV_FAULT_LIMIT, "VIN_OV_FAULT_LIMIT",
                float_2_slinear11(
                    tps546_config->vin_ov_fault_limit));
    VERIFY_BYTE(PMBUS_VIN_OV_FAULT_RESPONSE, "VIN_OV_FAULT_RESPONSE",
                tps546_config->vin_ov_fault_response);

#undef VERIFY_BYTE
#undef VERIFY_WORD
#undef VERIFY_BLOCK

    return ESP_OK;
}

int BONANZA_TPS546_get_temperature(void)
{
    uint16_t value = 0;
    int temp;

    if (smb_read_word(PMBUS_READ_TEMPERATURE_1, &value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read temperature");
        return last_temp;
    }

    temp = slinear11_2_int(value);
    last_temp = temp;
    return temp;
}

float BONANZA_TPS546_get_vin(void)
{
    uint16_t u16_value = 0;
    float vin;

    /* Get voltage input (ULINEAR16) */
    if (smb_read_word(PMBUS_READ_VIN, &u16_value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read VIN");
        return last_vin;
    } else {
        vin = slinear11_2_float(u16_value);
        #ifdef DEBUG_TPS546_MEAS
        ESP_LOGI(TAG, "Got Vin: %2.3f V", vin);
        #endif
        last_vin = vin;
        return vin;
    }
}

float BONANZA_TPS546_get_iout(void)
{
    uint16_t u16_value = 0;
    float iout;

    /* Get current output (SLINEAR11) */
    if (smb_read_word(PMBUS_READ_IOUT, &u16_value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read Iout");
        return last_iout;
    } else {
        iout = slinear11_2_float(u16_value);

    #ifdef DEBUG_TPS546_MEAS
        ESP_LOGI(TAG, "Got Iout: %2.3f A", iout);
    #endif
        last_iout = iout;
        return iout;
    }
}

float BONANZA_TPS546_get_vout(void)
{
    uint16_t u16_value = 0;
    float vout;

    /* Get voltage output (ULINEAR16) */
    if (smb_read_word(PMBUS_READ_VOUT, &u16_value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read Vout");
        return last_vout;
    } else {
        vout = ulinear16_2_float(u16_value);
    #ifdef DEBUG_TPS546_MEAS
        ESP_LOGI(TAG, "Got Vout: %2.3f V", vout);
    #endif
        last_vout = vout;
        return vout;
    }
}

esp_err_t BONANZA_TPS546_check_phase_currents(uint8_t phase_count, float minimum_current_a)
{
    uint8_t original_phase = 0;
    esp_err_t result = smb_read_byte(PMBUS_PHASE, &original_phase);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not read PHASE before buck participation test");
        return result;
    }

    for (uint8_t phase = 0; phase < phase_count; phase++) {
        uint16_t raw_iout = 0;

        result = smb_write_byte(PMBUS_PHASE, phase);
        if (result == ESP_OK) {
            result = smb_read_word(PMBUS_READ_IOUT, &raw_iout);
        }
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Buck phase %u current read failed: %s", phase, esp_err_to_name(result));
            break;
        }

        float current_a = slinear11_2_float(raw_iout);
        ESP_LOGI(TAG, "Buck phase %u current: %.3f A", phase, current_a);
        if (current_a < minimum_current_a) {
            ESP_LOGE(TAG, "Buck phase %u is not participating (%.3f A, minimum %.3f A)",
                     phase, current_a, minimum_current_a);
            result = ESP_FAIL;
            break;
        }
    }

    esp_err_t restore_result = smb_write_byte(PMBUS_PHASE, original_phase);
    if (restore_result != ESP_OK) {
        ESP_LOGE(TAG, "Could not restore PHASE=0x%02X after buck participation test: %s",
                 original_phase, esp_err_to_name(restore_result));
        return restore_result;
    }

    return result;
}

uint8_t BONANZA_TPS546_get_phase_count(void)
{
    uint16_t stack_config = 0;
    if (smb_read_word(PMBUS_STACK_CONFIG, &stack_config) == ESP_OK) {
        return (stack_config & 0x07) + 1;
    }
    return (tps546_config->stack_config & 0x07) + 1;
}
esp_err_t BONANZA_TPS546_check_status(GlobalState * GLOBAL_STATE) {

    SystemModule * SYSTEM_MODULE = &GLOBAL_STATE->SYSTEM_MODULE;
    uint16_t status;

    ESP_RETURN_ON_ERROR(smb_read_word(PMBUS_STATUS_WORD, &status), TAG, "Failed to read STATUS_WORD");

    if ((status & BONANZA_TPS546_STATUS_OFF) && xTaskGetTickCount() < tps546_power_good_grace_until) {
        uint8_t operation = 0;
        const uint16_t hard_faults = BONANZA_TPS546_STATUS_VOUT_OV | BONANZA_TPS546_STATUS_IOUT_OC | BONANZA_TPS546_STATUS_VIN_UV |
                                     BONANZA_TPS546_STATUS_TEMP | BONANZA_TPS546_STATUS_CML;
        if (!(status & hard_faults) && smb_read_byte(PMBUS_OPERATION, &operation) == ESP_OK &&
            operation == OPERATION_ON) {
            ESP_LOGI(TAG, "Waiting for TPS546 power-good after enable, STATUS_WORD: 0x%04X", status);
            return ESP_OK;
        }
    }

    //determine if this is a fault we care about
    if (status & (BONANZA_TPS546_STATUS_OFF | BONANZA_TPS546_STATUS_VOUT_OV | BONANZA_TPS546_STATUS_IOUT_OC | BONANZA_TPS546_STATUS_VIN_UV | BONANZA_TPS546_STATUS_TEMP)) {
        if (SYSTEM_MODULE->power_fault == 0) {
            BONANZA_TPS546_StatusSnapshot snapshot = {0};
            esp_err_t snapshot_err = BONANZA_TPS546_snapshot_status(&snapshot);
            if (snapshot_err == ESP_OK) {
                BONANZA_TPS546_log_snapshot(&snapshot);
            } else {
                ESP_LOGE(TAG, "Failed to snapshot TPS546 status: %s", esp_err_to_name(snapshot_err));
            }
            ESP_RETURN_ON_ERROR(BONANZA_TPS546_parse_status(status), TAG, "Failed to parse STATUS_WORD");
            SYSTEM_MODULE->power_fault = 1;
        }
    } else {
        SYSTEM_MODULE->power_fault = 0;
    }
    return ESP_OK;
}

// Global variable to store the TPS error message for the UI
static char tps_error_message[256] = "Power Fault Detected.";

const char* BONANZA_TPS546_get_error_message() {
    return tps_error_message;
}

static esp_err_t BONANZA_TPS546_parse_status(uint16_t status) {
    uint8_t u8_value;

    //print the status word
    ESP_LOGE(TAG, "Status: 0x%04X", status);

    if (status & BONANZA_TPS546_STATUS_BUSY) {
        ESP_LOGE(TAG, "Voltage regulator was busy and unable to respond");
        return ESP_OK;
    }

    if (status & BONANZA_TPS546_STATUS_OFF) {
        ESP_LOGE(TAG, "The voltage regulator is turned off");
    }

    if (status & BONANZA_TPS546_STATUS_VOUT_OV) {
        ESP_LOGE(TAG, "An output overvoltage fault has occurred");
    }

    if (status & BONANZA_TPS546_STATUS_IOUT_OC) {
        ESP_LOGE(TAG, "An output overcurrent fault has occurred");
    }

    if (status & BONANZA_TPS546_STATUS_VIN_UV) {
        ESP_LOGE(TAG, "An input undervoltage fault has occurred");
    }

    if (status & BONANZA_TPS546_STATUS_TEMP) {
        ESP_LOGE(TAG, "A temperature fault/warning has occurred");

        //the host should check STATUS_TEMPERATURE for more information.
        if (smb_read_byte(PMBUS_STATUS_TEMPERATURE, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_TEMPERATURE");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 Temperature Status: %02X", u8_value);
            if (u8_value & BONANZA_TPS546_STATUS_TEMP_OTF) {
                ESP_LOGE(TAG, "Overtemperature fault");
            }
            if (u8_value & BONANZA_TPS546_STATUS_TEMP_OTW) {
                ESP_LOGE(TAG, "Overtemperature warning");
            }
        }
    }

    if (status & BONANZA_TPS546_STATUS_CML) {
        ESP_LOGE(TAG, "A communication, memory, logic fault has occurred");

        //the host should check STATUS_CML for more information.
        if (smb_read_byte(PMBUS_STATUS_CML, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_CML");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 CML Status: %02X", u8_value);
            if (u8_value & BONANZA_TPS546_STATUS_CML_IVC) {
                ESP_LOGE(TAG, "invalid or unsupported command was received");
            }
            if (u8_value & BONANZA_TPS546_STATUS_CML_IVD) {
                ESP_LOGE(TAG, "invalid or unsupported data was received");
            }
            if (u8_value & BONANZA_TPS546_STATUS_CML_PEC) {
                ESP_LOGE(TAG, "packet error check has failed");
            }
            if (u8_value & BONANZA_TPS546_STATUS_CML_MEM) {
                ESP_LOGE(TAG, "memory error was detected");
            }
            if (u8_value & BONANZA_TPS546_STATUS_CML_PROC) {
                ESP_LOGE(TAG, "logic core error was detected");
            }
            if (u8_value & BONANZA_TPS546_STATUS_CML_COMM) {
                ESP_LOGE(TAG, "communication error detected");
            }
        }
    }

    if (status & BONANZA_TPS546_STATUS_NONE) {
        //ESP_LOGI(TAG, "TPS546 Status Word Error");
        //The host should check the STATUS_WORD for more information.
    }

    //STATUS_WORD bits

    if (status & BONANZA_TPS546_STATUS_VOUT) {
        //ESP_LOGI(TAG, "TPS546 VOUT Status Error");
        //the host should check STATUS_VOUT for more information.
        if (smb_read_byte(PMBUS_STATUS_VOUT, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_VOUT");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "VOUT Status: %02X", u8_value);

            if (u8_value & BONANZA_TPS546_STATUS_VOUT_OVF) {
                ESP_LOGE(TAG, "VOUT Overvoltage Fault");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VOUT_OVW) {
                ESP_LOGE(TAG, "VOUT Undervoltage Warning");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VOUT_UVW) {
                ESP_LOGE(TAG, "VOUT Undervoltage Warning");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VOUT_UVF) {
                ESP_LOGE(TAG, "VOUT Undervoltage Warning");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VOUT_MIN_MAX) {
                ESP_LOGE(TAG, "VOUT Outside Min/Max Range");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VOUT_TON_MAX) {
                ESP_LOGE(TAG, "VOUT Did not reach target output in time");
            }
        }
    }

    if (status & BONANZA_TPS546_STATUS_IOUT) {
        //ESP_LOGI(TAG, "TPS546 IOUT Status Error");
        //the host should check STATUS_IOUT for more information.
        if (smb_read_byte(PMBUS_STATUS_IOUT, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_IOUT");
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "TPS546 IOUT Status: %02X", u8_value);
            if (u8_value & BONANZA_TPS546_STATUS_IOUT_OCF) {
                ESP_LOGE(TAG, "IOUT Overcurrent Fault");
            }
            if (u8_value & BONANZA_TPS546_STATUS_IOUT_OCW) {
                ESP_LOGE(TAG, "IOUT Overcurrent Warning");
            }
        }
    }

    if (status & BONANZA_TPS546_STATUS_INPUT) {
        //ESP_LOGI(TAG, "TPS546 INPUT Status Error");
        //the host should check STATUS_INPUT for more information.
        if (smb_read_byte(PMBUS_STATUS_INPUT, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_INPUT");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 INPUT Status: %02X", u8_value);
            if (u8_value & BONANZA_TPS546_STATUS_VIN_OVF) {
                ESP_LOGE(TAG, "VIN Overvoltage Fault");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VIN_UVW) {
                ESP_LOGE(TAG, "VIN Undervoltage Warning");
            }
            if (u8_value & BONANZA_TPS546_STATUS_VIN_LOW_VIN) {
                ESP_LOGE(TAG, "VIN Low Voltage");
            }
        }
    }

    if (status & BONANZA_TPS546_STATUS_MFR) {
        //ESP_LOGI(TAG, "TPS546 MFR_SPECIFIC Status Error");
        //the host should check STATUS_MFR_SPECIFIC for more information.
        if (smb_read_byte(PMBUS_STATUS_MFR_SPECIFIC, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_MFR_SPECIFIC");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 MFR_SPECIFIC Status: %02X", u8_value);
            if (u8_value & BONANZA_TPS546_STATUS_MFR_POR) {
                ESP_LOGE(TAG, "A Power-On Reset Fault has been detected.");
            }
            if (u8_value & BONANZA_TPS546_STATUS_MFR_SELF) {
                ESP_LOGE(TAG, "Power-On Self-Check is in progress. One or more BCX slaves have not responded.");
            }
            if (u8_value & BONANZA_TPS546_STATUS_MFR_RESET) {
                ESP_LOGE(TAG, "A RESET_VOUT event has occurred.");
            }
            if (u8_value & BONANZA_TPS546_STATUS_MFR_BCX) {
                ESP_LOGE(TAG, "A BCX fault event has occurred.");
            }
            if (u8_value & BONANZA_TPS546_STATUS_MFR_SYNC) {
                ESP_LOGE(TAG, "A SYNC fault has been detected.");
            }
        }
    }

    if (status & BONANZA_TPS546_STATUS_PGOOD) {
        ESP_LOGE(TAG, "The output voltage is NOT within the regulation window. PGOOD pin is asserted.");
    }

    if (status & BONANZA_TPS546_STATUS_OTHER) {
        //ESP_LOGI(TAG, "TPS546 OTHER Status Error");
        //the host should check STATUS_OTHER for more information.
        if (smb_read_byte(PMBUS_STATUS_OTHER, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_OTHER");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 OTHER Status: %02X", u8_value);
            if (u8_value & BONANZA_TPS546_STATUS_OTHER_FIRST) {
                ESP_LOGE(TAG, "this device was the first to assert SMBALERT");
            }
        }
    }

    return ESP_OK;
}

/**
 * @brief Sets the core voltage
 * This function controls the regulator output state within the board profile limits.
 * send a 0 to turn off the output
 * @param volts The desired output voltage
**/
esp_err_t BONANZA_TPS546_set_vout(float volts) {
    uint16_t value;
    uint8_t value8;

    if (volts == 0) {
        /* turn off output */
        if (smb_write_byte(PMBUS_OPERATION, OPERATION_OFF) != ESP_OK) {
            ESP_LOGE(TAG, "Could not turn off Vout");
            return ESP_FAIL;
        }
        tps546_power_good_grace_until = 0;
    } else {
        /* make sure we're in range */
        if ((volts < tps546_config->vout_min) || (volts > tps546_config->vout_max)) {
            ESP_LOGE(TAG, "Voltage requested (%f V) is out of range", volts);
            return ESP_FAIL;
        } else {
            /* set the output voltage */
            value = float_2_ulinear16(volts);
            if (smb_write_word(PMBUS_VOUT_COMMAND, value) != ESP_OK) {
                ESP_LOGE(TAG, "Could not set Vout to %1.2f V", volts);
                return ESP_FAIL;
            }

            ESP_LOGI(TAG, "Vout changed to %1.2f V", volts);

            /* turn on output */
            if (smb_write_byte(PMBUS_OPERATION, OPERATION_ON) != ESP_OK) {
                ESP_LOGE(TAG, "Could not turn on Vout");
                return ESP_FAIL;
            }
            tps546_power_good_grace_until = xTaskGetTickCount() + pdMS_TO_TICKS(BONANZA_TPS546_POWER_GOOD_GRACE_MS);

            //make sure operation was written correctly
            if (smb_read_byte(PMBUS_OPERATION, &value8) != ESP_OK) {
                ESP_LOGE(TAG, "Could not read OPERATION");
                return ESP_FAIL;
            }

            if (value8 != OPERATION_ON) {
                ESP_LOGE(TAG, "Operation not set to ON: %02X", value8);
            }

        }
    }
    return ESP_OK;
}

esp_err_t BONANZA_TPS546_snapshot_status(BONANZA_TPS546_StatusSnapshot *s)
{
    if (s == NULL) return ESP_ERR_INVALID_ARG;
    uint16_t raw;
    uint8_t mode;
    esp_err_t err;

    /* Only live operating data belongs in the periodic power-health sample.
     * Startup checks the fixed protection registers; fault decoding reads
     * detailed status bytes on demand. */
    err = smb_read_word(PMBUS_STATUS_WORD, &s->status_word);
    if (err != ESP_OK) return err;
    err = smb_read_byte(PMBUS_OPERATION, &s->operation);
    if (err != ESP_OK) return err;
    err = smb_read_byte(PMBUS_VOUT_MODE, &mode);
    if (err != ESP_OK) return err;
    err = smb_read_word(PMBUS_VOUT_COMMAND, &raw);
    if (err != ESP_OK) return err;
    s->vout_command_raw = raw;
    s->vout_command = ulinear16_2_float_mode(raw, mode);
    s->vout_command_matches_active_config =
        raw == float_2_ulinear16_mode(tps546_config->vout_command, tps546_vout_mode);

    err = smb_read_word(PMBUS_READ_VOUT, &raw);
    if (err != ESP_OK) return err;
    s->read_vout = ulinear16_2_float_mode(raw, mode);
    err = smb_read_word(PMBUS_READ_VIN, &raw);
    if (err != ESP_OK) return err;
    s->read_vin = slinear11_2_float(raw);
    err = smb_read_word(PMBUS_READ_IOUT, &raw);
    if (err != ESP_OK) return err;
    s->read_iout = slinear11_2_float(raw);
    err = smb_read_word(PMBUS_READ_TEMPERATURE_1, &raw);
    if (err != ESP_OK) return err;
    s->read_temp1 = slinear11_2_int(raw);
    return ESP_OK;
}

void BONANZA_TPS546_log_snapshot(const BONANZA_TPS546_StatusSnapshot *s)
{
    ESP_LOGE(TAG, "TPS546 status=0x%04X operation=0x%02X command=%.3fV exact=%u",
             s->status_word, s->operation, s->vout_command,
             (unsigned)s->vout_command_matches_active_config);
    ESP_LOGE(TAG, "TPS546 VIN=%.3fV VOUT=%.3fV IOUT=%.3fA temperature=%dC",
             s->read_vin, s->read_vout, s->read_iout, s->read_temp1);
}
