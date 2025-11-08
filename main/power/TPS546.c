#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "pmbus_commands.h"

#include "i2c_bitaxe.h"
#include "TPS546.h"

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

static const char *TAG = "TPS546";

static uint8_t DEVICE_ID_TPS546D24A[] = {0x54, 0x49, 0x54, 0x6D, 0x24, 0x41};
static uint8_t DEVICE_ID_TPS546D24S[] = {0x54, 0x49, 0x54, 0x6D, 0x24, 0x62};
// static uint8_t DEVICE_ID_TPS546B24A[] = {0x54, 0x49, 0x54, 0x6B, 0x24, 0x41};
// static uint8_t DEVICE_ID_TPS546B24S[] = {0x54, 0x49, 0x54, 0x6B, 0x24, 0x62};

static i2c_master_dev_handle_t tps546_i2c_handle;

static TPS546_CONFIG tps546_config;

static esp_err_t TPS546_parse_status(uint16_t);

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
    if (i2c_bitaxe_register_read(tps546_i2c_handle, command, buf, len+1) != ESP_OK) {
        free(buf);
        return ESP_FAIL;
    }
    //copy the data into the buffer
    memcpy(data, buf+1, len);
    free(buf);

    return ESP_OK;
}

// /**
//  * @brief SMBus write block - don;t forget the length byte first :P
//  * @param command The command to write
//  * @param data The data to write
//  * @param len The number of bytes to write
//  */
// static esp_err_t smb_write_block(uint8_t command, uint8_t *data, uint8_t len)
// {
//     //malloc a buffer len+2 to store the command byte and then the length byte
//     uint8_t *buf = (uint8_t *)malloc(len+2);
//     buf[0] = command;
//     buf[1] = len;
//     //copy the data into the buffer
//     memcpy(buf+2, data, len);

//     //write it all
//     if (i2c_bitaxe_register_write_bytes(tps546_i2c_handle, buf, len+2) != ESP_OK) {
//         free(buf);
//         return ESP_FAIL;
//     } else {
//         free(buf);
//         return ESP_OK;
//     }
// }

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
static float ulinear16_2_float(uint16_t value)
{
    uint8_t voutmode;
    int exponent;
    float result;

    smb_read_byte(PMBUS_VOUT_MODE, &voutmode);

    if (voutmode & 0x10) {
        // exponent is negative
        exponent = -1 * ((~voutmode & 0x1F) + 1);
    } else {
        exponent = (voutmode & 0x1F);
    }
    result = (value * powf(2.0, exponent));
    return result;
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

/*--- Public TPS546 functions ---*/

/**
 * @brief Set up the TPS546 regulator and turn it on
*/
esp_err_t TPS546_init(TPS546_CONFIG config)
{
    uint8_t u8_value = 0;
    uint16_t u16_value = 0;
    uint8_t read_mfr_revision[4];
    int temp;
    uint8_t comp_config[5];
    uint8_t voutmode;

    tps546_config = config;

    ESP_LOGI(TAG, "Initializing the core voltage regulator");
    ESP_RETURN_ON_ERROR(i2c_bitaxe_add_device(TPS546_I2CADDR, &tps546_i2c_handle, TAG), TAG, "Failed to add TPS546 I2C");

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
    smb_write_byte(PMBUS_OPERATION, u8_value);

    /* Make sure power is turned off until commanded */
    u8_value = (ON_OFF_CONFIG_DELAY | ON_OFF_CONFIG_POLARITY | ON_OFF_CONFIG_CP | ON_OFF_CONFIG_CMD | ON_OFF_CONFIG_PU);
    ESP_LOGI(TAG, "Power config-ON_OFF_CONFIG: %02X", u8_value);
    smb_write_byte(PMBUS_ON_OFF_CONFIG, u8_value);

    /* Read version number and see if it matches */
    TPS546_read_mfr_info(read_mfr_revision);
    // if (memcmp(read_mfr_revision, MFR_REVISION, 3) != 0) {
    
    // If it doesn't match, then write all the registers and set new version number
    // ESP_LOGI(TAG, "--------------------------------");
    // ESP_LOGI(TAG, "Config version mismatch, writing new config values");
    ESP_LOGI(TAG, "Writing new config values");
    smb_read_byte(PMBUS_VOUT_MODE, &voutmode);
    ESP_LOGI(TAG, "VOUT_MODE: %02x", voutmode);
    TPS546_write_entire_config();
    //}

    // /* Show temperature */
    // ESP_LOGI(TAG, "--------------------------------");
    // ESP_LOGI(TAG, "Temp: %d", TPS546_get_temperature());

    // /* Show switching frequency */
    // TPS546_get_frequency();
    // TPS546_set_frequency(650);

    /* Show voltage settings */
    TPS546_show_voltage_settings();

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
    TPS546_clear_faults();

    smb_read_word(PMBUS_STATUS_WORD, &u16_value);
    ESP_LOGI(TAG, "read STATUS_WORD: %04x", u16_value);

    return ESP_OK;
}

esp_err_t TPS546_clear_faults(void) {

    ESP_RETURN_ON_ERROR(smb_write_addr(PMBUS_CLEAR_FAULTS), TAG, "Failed to write address");

    // acknowledge the SMBus fault to reset the SMBALERT pin
    //ESP_RETURN_ON_ERROR(smb_clear_alert(), TAG, "Failed to clear alert"); //this doesn't seem to work?

    return ESP_OK;
}

/**
 * @brief Read the manufacturer model and revision 
 * @param read_mfr_revision Pointer to store the read revision
*/
void TPS546_read_mfr_info(uint8_t *read_mfr_revision)
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

/**
 * @brief Set all the relevant config registers for normal operation 
*/
void TPS546_write_entire_config(void)
{
    ESP_LOGI(TAG, "---Writing new config values to TPS546---");
    /* set up the ON_OFF_CONFIG */
    // ESP_LOGI(TAG, "Setting ON_OFF_CONFIG: %02X", TPS546_INIT_ON_OFF_CONFIG);
    // if (smb_write_byte(PMBUS_ON_OFF_CONFIG, TPS546_INIT_ON_OFF_CONFIG) != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to write ON_OFF_CONFIG");
    //     return;
    // }

    /* Phase */
    ESP_LOGI(TAG, "Setting PHASE: %02X", TPS546_INIT_PHASE);
    smb_write_byte(PMBUS_PHASE, TPS546_INIT_PHASE);

    /* Switch frequency */
    ESP_LOGI(TAG, "Setting FREQUENCY: %dMHz", TPS546_INIT_FREQUENCY);
    smb_write_word(PMBUS_FREQUENCY_SWITCH, int_2_slinear11(TPS546_INIT_FREQUENCY));

    /* vin voltage */

    //deal with the UV_WARN_LIMIT bug
    if (tps546_config.TPS546_INIT_VIN_UV_WARN_LIMIT > 0) {
        ESP_LOGI(TAG, "Setting VIN_UV_WARN_LIMIT: %.2f", tps546_config.TPS546_INIT_VIN_UV_WARN_LIMIT);
        smb_write_word(PMBUS_VIN_UV_WARN_LIMIT, float_2_slinear11(tps546_config.TPS546_INIT_VIN_UV_WARN_LIMIT));
    }

    ESP_LOGI(TAG, "Setting VIN_ON: %.2fV", tps546_config.TPS546_INIT_VIN_ON);
    smb_write_word(PMBUS_VIN_ON, float_2_slinear11(tps546_config.TPS546_INIT_VIN_ON));

    ESP_LOGI(TAG, "Setting VIN_OFF: %.2fV", tps546_config.TPS546_INIT_VIN_OFF);
    smb_write_word(PMBUS_VIN_OFF, float_2_slinear11(tps546_config.TPS546_INIT_VIN_OFF));

    ESP_LOGI(TAG, "Setting VIN_OV_FAULT_LIMIT: %.2fV", tps546_config.TPS546_INIT_VIN_OV_FAULT_LIMIT);
    smb_write_word(PMBUS_VIN_OV_FAULT_LIMIT, float_2_slinear11(tps546_config.TPS546_INIT_VIN_OV_FAULT_LIMIT));

    ESP_LOGI(TAG, "Setting VIN_OV_FAULT_RESPONSE: %02X", TPS546_INIT_VIN_OV_FAULT_RESPONSE);
    smb_write_byte(PMBUS_VIN_OV_FAULT_RESPONSE, TPS546_INIT_VIN_OV_FAULT_RESPONSE);

    /* vout voltage */
    ESP_LOGI(TAG, "Setting VOUT SCALE: %.2f", tps546_config.TPS546_INIT_SCALE_LOOP);
    smb_write_word(PMBUS_VOUT_SCALE_LOOP, float_2_slinear11(tps546_config.TPS546_INIT_SCALE_LOOP));

    ESP_LOGI(TAG, "Setting VOUT_COMMAND: %.2fV", tps546_config.TPS546_INIT_VOUT_COMMAND);
    smb_write_word(PMBUS_VOUT_COMMAND, float_2_ulinear16(tps546_config.TPS546_INIT_VOUT_COMMAND));

    ESP_LOGI(TAG, "Setting VOUT_MAX: %.2fV", tps546_config.TPS546_INIT_VOUT_MAX);
    smb_write_word(PMBUS_VOUT_MAX, float_2_ulinear16(tps546_config.TPS546_INIT_VOUT_MAX));

    ESP_LOGI(TAG, "Setting VOUT_MIN: %.2fV", tps546_config.TPS546_INIT_VOUT_MIN);
    smb_write_word(PMBUS_VOUT_MIN, float_2_ulinear16(tps546_config.TPS546_INIT_VOUT_MIN));

    ESP_LOGI(TAG, "Setting VOUT_OV_FAULT_LIMIT: %.2f", TPS546_INIT_VOUT_OV_FAULT_LIMIT);
    smb_write_word(PMBUS_VOUT_OV_FAULT_LIMIT, float_2_ulinear16(TPS546_INIT_VOUT_OV_FAULT_LIMIT));

    ESP_LOGI(TAG, "Setting VOUT_OV_WARN_LIMIT: %.2f", TPS546_INIT_VOUT_OV_WARN_LIMIT);
    smb_write_word(PMBUS_VOUT_OV_WARN_LIMIT, float_2_ulinear16(TPS546_INIT_VOUT_OV_WARN_LIMIT));

    ESP_LOGI(TAG, "Setting VOUT_MARGIN_HIGH: %.2f", TPS546_INIT_VOUT_MARGIN_HIGH);
    smb_write_word(PMBUS_VOUT_MARGIN_HIGH, float_2_ulinear16(TPS546_INIT_VOUT_MARGIN_HIGH));

    ESP_LOGI(TAG, "Setting VOUT_MARGIN_LOW: %.2f", TPS546_INIT_VOUT_MARGIN_LOW);
    smb_write_word(PMBUS_VOUT_MARGIN_LOW, float_2_ulinear16(TPS546_INIT_VOUT_MARGIN_LOW));

    ESP_LOGI(TAG, "Setting VOUT_UV_WARN_LIMIT: %.2f", TPS546_INIT_VOUT_UV_WARN_LIMIT);
    smb_write_word(PMBUS_VOUT_UV_WARN_LIMIT, float_2_ulinear16(TPS546_INIT_VOUT_UV_WARN_LIMIT));

    ESP_LOGI(TAG, "Setting VOUT_UV_FAULT_LIMIT: %.2f", TPS546_INIT_VOUT_UV_FAULT_LIMIT);
    smb_write_word(PMBUS_VOUT_UV_FAULT_LIMIT, float_2_ulinear16(TPS546_INIT_VOUT_UV_FAULT_LIMIT));

    /* iout current */
    ESP_LOGI(TAG, "----- IOUT");
    ESP_LOGI(TAG, "Setting IOUT_OC_WARN_LIMIT: %.2fA", tps546_config.TPS546_INIT_IOUT_OC_WARN_LIMIT);
    smb_write_word(PMBUS_IOUT_OC_WARN_LIMIT, float_2_slinear11(tps546_config.TPS546_INIT_IOUT_OC_WARN_LIMIT));

    ESP_LOGI(TAG, "Setting IOUT_OC_FAULT_LIMIT: %.2fA", tps546_config.TPS546_INIT_IOUT_OC_FAULT_LIMIT);
    smb_write_word(PMBUS_IOUT_OC_FAULT_LIMIT, float_2_slinear11(tps546_config.TPS546_INIT_IOUT_OC_FAULT_LIMIT));

    ESP_LOGI(TAG, "Setting IOUT_OC_FAULT_RESPONSE: %02x", TPS546_INIT_IOUT_OC_FAULT_RESPONSE);
    smb_write_byte(PMBUS_IOUT_OC_FAULT_RESPONSE, TPS546_INIT_IOUT_OC_FAULT_RESPONSE);

    /* temperature */
    ESP_LOGI(TAG, "----- TEMPERATURE");
    ESP_LOGI(TAG, "Setting OT_WARN_LIMIT: %dC", TPS546_INIT_OT_WARN_LIMIT);
    smb_write_word(PMBUS_OT_WARN_LIMIT, int_2_slinear11(TPS546_INIT_OT_WARN_LIMIT));
    ESP_LOGI(TAG, "Setting OT_FAULT_LIMIT: %dC", TPS546_INIT_OT_FAULT_LIMIT);
    smb_write_word(PMBUS_OT_FAULT_LIMIT, int_2_slinear11(TPS546_INIT_OT_FAULT_LIMIT));
    ESP_LOGI(TAG, "Setting OT_FAULT_RESPONSE: %02x", TPS546_INIT_OT_FAULT_RESPONSE);
    smb_write_byte(PMBUS_OT_FAULT_RESPONSE, TPS546_INIT_OT_FAULT_RESPONSE);

    /* timing */
    ESP_LOGI(TAG, "----- TIMING");
    ESP_LOGI(TAG, "Setting TON_DELAY: %dms", TPS546_INIT_TON_DELAY);
    smb_write_word(PMBUS_TON_DELAY, int_2_slinear11(TPS546_INIT_TON_DELAY));
    ESP_LOGI(TAG, "Setting TON_RISE: %dms", TPS546_INIT_TON_RISE);
    smb_write_word(PMBUS_TON_RISE, int_2_slinear11(TPS546_INIT_TON_RISE));
    ESP_LOGI(TAG, "Setting TON_MAX_FAULT_LIMIT: %dms", TPS546_INIT_TON_MAX_FAULT_LIMIT);
    smb_write_word(PMBUS_TON_MAX_FAULT_LIMIT, int_2_slinear11(TPS546_INIT_TON_MAX_FAULT_LIMIT));
    ESP_LOGI(TAG, "Setting TON_MAX_FAULT_RESPONSE: %02x", TPS546_INIT_TON_MAX_FAULT_RESPONSE);
    smb_write_byte(PMBUS_TON_MAX_FAULT_RESPONSE, TPS546_INIT_TON_MAX_FAULT_RESPONSE);
    ESP_LOGI(TAG, "Setting TOFF_DELAY: %dms", TPS546_INIT_TOFF_DELAY);
    smb_write_word(PMBUS_TOFF_DELAY, int_2_slinear11(TPS546_INIT_TOFF_DELAY));
    ESP_LOGI(TAG, "Setting TOFF_FALL: %dms", TPS546_INIT_TOFF_FALL);
    smb_write_word(PMBUS_TOFF_FALL, int_2_slinear11(TPS546_INIT_TOFF_FALL));

    /* Compensation config */
    //ESP_LOGI(TAG, "COMPENSATION");
    //smb_write_block(PMBUS_COMPENSATION_CONFIG, COMPENSATION_CONFIG, 5);

    /* configure the bootup behavior regarding pin detect values vs NVM values */
    ESP_LOGI(TAG, "Setting PIN_DETECT_OVERRIDE");
    smb_write_word(PMBUS_PIN_DETECT_OVERRIDE, INIT_PIN_DETECT_OVERRIDE);

    /* TODO write new MFR_REVISION number to reflect these parameters */
    // ESP_LOGI(TAG, "Setting MFR ID");
    // smb_write_block(PMBUS_MFR_ID, MFR_ID, 3);
    // ESP_LOGI(TAG, "Setting MFR MODEL");
    // smb_write_block(PMBUS_MFR_ID, MFR_MODEL, 3);
    // ESP_LOGI(TAG, "Setting MFR REVISION");
    // smb_write_block(PMBUS_MFR_ID, MFR_REVISION, 3);

    /*
    !!!!!!!!!!!!!!!!!!!!!!!!!!!
    // Never write this to NVM as it can corrupt the TPS in an unrecoverable state, just do it on boot every time
    !!!!!!!!!!!!!!!!!!!!!!!!!!!
    */
    /* store configuration in NVM */
    // ESP_LOGI(TAG, "---Saving new config---");
    // smb_write_byte(PMBUS_STORE_USER_ALL, 0x98);

}

int TPS546_get_frequency(void)
{
    uint16_t value = 0;
    int freq;

    smb_read_word(PMBUS_FREQUENCY_SWITCH, &value);
    freq = slinear11_2_int(value);

    return (int)freq;
}

void TPS546_set_frequency(int newfreq)
{
    uint16_t value = 0;
    //int freq;

    ESP_LOGI(TAG, "Writing new frequency: %d", newfreq);
    value = int_2_slinear11(newfreq);
    //ESP_LOGI(TAG, "New value: 0x%04x", value);
    smb_write_word(PMBUS_FREQUENCY_SWITCH, value);

    //ESP_LOGI(TAG, "Checking conversion...");
    //freq = slinear11_2_int(value);
    //ESP_LOGI(TAG, "Converted value: %d", freq);
}

int TPS546_get_temperature(void)
{
    uint16_t value = 0;
    int temp;

    smb_read_word(PMBUS_READ_TEMPERATURE_1, &value);
    temp = slinear11_2_int(value);
    return temp;
}

float TPS546_get_vin(void)
{
    uint16_t u16_value = 0;
    float vin;

    /* Get voltage input (ULINEAR16) */
    if (smb_read_word(PMBUS_READ_VIN, &u16_value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read VIN");
        return 0;
    } else {
        vin = slinear11_2_float(u16_value);
        #ifdef DEBUG_TPS546_MEAS
        ESP_LOGI(TAG, "Got Vin: %2.3f V", vin);
        #endif
        return vin;
    }    
}

float TPS546_get_iout(void)
{
    uint16_t u16_value = 0;
    float iout;

    //set the phase register to 0xFF to read all phases
    smb_write_byte(PMBUS_PHASE, 0xFF);

    /* Get current output (SLINEAR11) */
    if (smb_read_word(PMBUS_READ_IOUT, &u16_value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read Iout");
        return 0;
    } else {
        iout = slinear11_2_float(u16_value);

    #ifdef DEBUG_TPS546_MEAS
        ESP_LOGI(TAG, "Got Iout: %2.3f A", iout);
    #endif

    //set the phase register back to the default
    smb_write_byte(PMBUS_PHASE, TPS546_INIT_PHASE);

        return iout;
    }
}

float TPS546_get_vout(void)
{
    uint16_t u16_value = 0;
    float vout;

    /* Get voltage output (ULINEAR16) */
    if (smb_read_word(PMBUS_READ_VOUT, &u16_value) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read Vout");
        return 0;
    } else {
        vout = ulinear16_2_float(u16_value);
    #ifdef DEBUG_TPS546_MEAS
        ESP_LOGI(TAG, "Got Vout: %2.3f V", vout);
    #endif
        return vout;
    }
}

esp_err_t TPS546_check_status(GlobalState * GLOBAL_STATE) {

    SystemModule * SYSTEM_MODULE = &GLOBAL_STATE->SYSTEM_MODULE;
    uint16_t status;

    ESP_RETURN_ON_ERROR(smb_read_word(PMBUS_STATUS_WORD, &status), TAG, "Failed to read STATUS_WORD");
    //determine if this is a fault we care about
    if (status & (TPS546_STATUS_OFF | TPS546_STATUS_VOUT_OV | TPS546_STATUS_IOUT_OC | TPS546_STATUS_VIN_UV | TPS546_STATUS_TEMP)) {
        if (SYSTEM_MODULE->power_fault == 0) {
            ESP_RETURN_ON_ERROR(TPS546_parse_status(status), TAG, "Failed to parse STATUS_WORD");
            SYSTEM_MODULE->power_fault = 1;
        }
    } else {
        SYSTEM_MODULE->power_fault = 0;
    }
    return ESP_OK;
}

// Global variable to store the TPS error message for the UI
static char tps_error_message[256] = "Power Fault Detected.";

const char* TPS546_get_error_message() {
    return tps_error_message;
}


static esp_err_t TPS546_parse_status(uint16_t status) {
    uint8_t u8_value;

    //print the status word
    ESP_LOGE(TAG, "Status: 0x%04X", status);

    if (status & TPS546_STATUS_BUSY) {
        ESP_LOGE(TAG, "Voltage regulator was busy and unable to respond");
        return ESP_OK;
    }
    
    if (status & TPS546_STATUS_OFF) {
        ESP_LOGE(TAG, "The voltage regulator is turned off");
    }
    
    if (status & TPS546_STATUS_VOUT_OV) {
        ESP_LOGE(TAG, "An output overvoltage fault has occurred");
    }
    
    if (status & TPS546_STATUS_IOUT_OC) {
        ESP_LOGE(TAG, "An output overcurrent fault has occurred");
    }
    
    if (status & TPS546_STATUS_VIN_UV) {
        ESP_LOGE(TAG, "An input undervoltage fault has occurred");
    }
    
    if (status & TPS546_STATUS_TEMP) {
        ESP_LOGE(TAG, "A temperature fault/warning has occurred");

        //the host should check STATUS_TEMPERATURE for more information.
        if (smb_read_byte(PMBUS_STATUS_TEMPERATURE, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_TEMPERATURE");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 Temperature Status: %02X", u8_value);
            if (u8_value & TPS546_STATUS_TEMP_OTF) {
                ESP_LOGE(TAG, "Overtemperature fault");
            }
            if (u8_value & TPS546_STATUS_TEMP_OTW) {
                ESP_LOGE(TAG, "Overtemperature warning");
            }
        }
    }
    
    if (status & TPS546_STATUS_CML) {
        ESP_LOGE(TAG, "A communication, memory, logic fault has occurred");

        //the host should check STATUS_CML for more information.
        if (smb_read_byte(PMBUS_STATUS_CML, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_CML");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 CML Status: %02X", u8_value);
            if (u8_value & TPS546_STATUS_CML_IVC) {
                ESP_LOGE(TAG, "invalid or unsupported command was received");
            }
            if (u8_value & TPS546_STATUS_CML_IVD) {
                ESP_LOGE(TAG, "invalid or unsupported data was received");
            }
            if (u8_value & TPS546_STATUS_CML_PEC) {
                ESP_LOGE(TAG, "packet error check has failed");
            }
            if (u8_value & TPS546_STATUS_CML_MEM) {
                ESP_LOGE(TAG, "memory error was detected");
            }
            if (u8_value & TPS546_STATUS_CML_PROC) {
                ESP_LOGE(TAG, "logic core error was detected");
            }
            if (u8_value & TPS546_STATUS_CML_COMM) {
                ESP_LOGE(TAG, "communication error detected");
            }
        }
    }
    
    if (status & TPS546_STATUS_NONE) {
        //ESP_LOGI(TAG, "TPS546 Status Word Error");
        //The host should check the STATUS_WORD for more information.
    } else {
        return ESP_OK;
    }

    //STATUS_WORD bits

    if (status & TPS546_STATUS_VOUT) {
        //ESP_LOGI(TAG, "TPS546 VOUT Status Error");
        //the host should check STATUS_VOUT for more information.
        if (smb_read_byte(PMBUS_STATUS_VOUT, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_VOUT");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "VOUT Status: %02X", u8_value);

            if (u8_value & TPS546_STATUS_VOUT_OVF) {
                ESP_LOGE(TAG, "VOUT Overvoltage Fault");
            }
            if (u8_value & TPS546_STATUS_VOUT_OVW) {
                ESP_LOGE(TAG, "VOUT Undervoltage Warning");
            }
            if (u8_value & TPS546_STATUS_VOUT_UVW) {
                ESP_LOGE(TAG, "VOUT Undervoltage Warning");
            }
            if (u8_value & TPS546_STATUS_VOUT_UVF) {
                ESP_LOGE(TAG, "VOUT Undervoltage Warning");
            }
            if (u8_value & TPS546_STATUS_VOUT_MIN_MAX) {
                ESP_LOGE(TAG, "VOUT Outside Min/Max Range");
            }
            if (u8_value & TPS546_STATUS_VOUT_TON_MAX) {
                ESP_LOGE(TAG, "VOUT Did not reach target output in time");
            }
        }
    }

    if (status & TPS546_STATUS_IOUT) {
        //ESP_LOGI(TAG, "TPS546 IOUT Status Error");
        //the host should check STATUS_IOUT for more information.
        if (smb_read_byte(PMBUS_STATUS_IOUT, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_IOUT");
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "TPS546 IOUT Status: %02X", u8_value);
            if (u8_value & TPS546_STATUS_IOUT_OCF) {
                ESP_LOGE(TAG, "IOUT Overcurrent Fault");
            }
            if (u8_value & TPS546_STATUS_IOUT_OCW) {
                ESP_LOGE(TAG, "IOUT Overcurrent Warning");
            }
        }
    }

    if (status & TPS546_STATUS_INPUT) {
        //ESP_LOGI(TAG, "TPS546 INPUT Status Error");
        //the host should check STATUS_INPUT for more information.
        if (smb_read_byte(PMBUS_STATUS_INPUT, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_INPUT");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 INPUT Status: %02X", u8_value);
            if (u8_value & TPS546_STATUS_VIN_OVF) {
                ESP_LOGE(TAG, "VIN Overvoltage Fault");
            }
            if (u8_value & TPS546_STATUS_VIN_UVW) {
                ESP_LOGE(TAG, "VIN Undervoltage Warning");
            }
            if (u8_value & TPS546_STATUS_VIN_LOW_VIN) {
                ESP_LOGE(TAG, "VIN Low Voltage");
            }
        }
    }

    if (status & TPS546_STATUS_MFR) {
        //ESP_LOGI(TAG, "TPS546 MFR_SPECIFIC Status Error");
        //the host should check STATUS_MFR_SPECIFIC for more information.
        if (smb_read_byte(PMBUS_STATUS_MFR_SPECIFIC, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_MFR_SPECIFIC");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 MFR_SPECIFIC Status: %02X", u8_value);
            if (u8_value & TPS546_STATUS_MFR_POR) {
                ESP_LOGE(TAG, "A Power-On Reset Fault has been detected.");
            }
            if (u8_value & TPS546_STATUS_MFR_SELF) {
                ESP_LOGE(TAG, "Power-On Self-Check is in progress. One or more BCX slaves have not responded.");
            }
            if (u8_value & TPS546_STATUS_MFR_RESET) {
                ESP_LOGE(TAG, "A RESET_VOUT event has occurred.");
            }
            if (u8_value & TPS546_STATUS_MFR_BCX) {
                ESP_LOGE(TAG, "A BCX fault event has occurred.");
            }
            if (u8_value & TPS546_STATUS_MFR_SYNC) {
                ESP_LOGE(TAG, "A SYNC fault has been detected.");
            }
        }
    }

    if (status & TPS546_STATUS_PGOOD) {
        ESP_LOGE(TAG, "The output voltage is NOT within the regulation window. PGOOD pin is asserted.");
    }

    if (status & TPS546_STATUS_OTHER) {
        //ESP_LOGI(TAG, "TPS546 OTHER Status Error");
        //the host should check STATUS_OTHER for more information.
        if (smb_read_byte(PMBUS_STATUS_OTHER, &u8_value) != ESP_OK) {
            ESP_LOGE(TAG, "Could not read STATUS_OTHER");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "TPS546 OTHER Status: %02X", u8_value);
            if (u8_value & TPS546_STATUS_OTHER_FIRST) {
                ESP_LOGE(TAG, "this device was the first to assert SMBALERT");
            }
        }
    }

    return ESP_OK;
}

/**
 * @brief Sets the core voltage
 * this function controls the regulator ontput state
 * send it the desired output in millivolts
 * A value between TPS546_INIT_VOUT_MIN and TPS546_INIT_VOUT_MAX
 * send a 0 to turn off the output
 * @param volts The desired output voltage
**/
esp_err_t TPS546_set_vout(float volts) {
    uint16_t value;
    uint8_t value8;

    if (volts == 0) {
        /* turn off output */
        if (smb_write_byte(PMBUS_OPERATION, OPERATION_OFF) != ESP_OK) {
            ESP_LOGE(TAG, "Could not turn off Vout");
            return ESP_FAIL;
        }
    } else {
        /* make sure we're in range */
        if ((volts < tps546_config.TPS546_INIT_VOUT_MIN) || (volts > tps546_config.TPS546_INIT_VOUT_MAX)) {
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

void TPS546_show_voltage_settings(void)
{
    uint16_t u16_value = 0;
    uint8_t u8_value;
    float f_value;

    ESP_LOGI(TAG, "-----------VOLTAGE---------------------");
    /* VIN_ON SLINEAR11 */
    smb_read_word(PMBUS_VIN_ON, &u16_value);
    f_value = slinear11_2_float(u16_value);
    ESP_LOGI(TAG, "read VIN_ON: %.2fV", f_value);

    /* VIN_OFF SLINEAR11 */
    smb_read_word(PMBUS_VIN_OFF, &u16_value);
    f_value = slinear11_2_float(u16_value);
    ESP_LOGI(TAG, "read VIN_OFF: %.2fV", f_value);

    /* VIN_OV_FAULT_LIMIT SLINEAR11 */
    smb_read_word(PMBUS_VIN_OV_FAULT_LIMIT, &u16_value);
    f_value = slinear11_2_float(u16_value);
    ESP_LOGI(TAG, "read VIN_OV_FAULT_LIMIT: %.2fV", f_value);

    /* VIN_UV_WARN_LIMIT SLINEAR11 */
    smb_read_word(PMBUS_VIN_UV_WARN_LIMIT, &u16_value);
    f_value = slinear11_2_float(u16_value);
    ESP_LOGI(TAG, "read VIN_UV_WARN_LIMIT: %.2fV", f_value);

    /* VIN_OV_FAULT_RESPONSE */
    smb_read_byte(PMBUS_VIN_OV_FAULT_RESPONSE, &u8_value);
    ESP_LOGI(TAG, "read VIN_OV_FAULT_RESPONSE: %02X", u8_value);

    /* VOUT_MAX */
    smb_read_word(PMBUS_VOUT_MAX, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_MAX: %.2fV", f_value);

    /* VOUT_OV_FAULT_LIMIT */
    smb_read_word(PMBUS_VOUT_OV_FAULT_LIMIT, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_OV_FAULT_LIMIT: %.2fV", f_value * tps546_config.TPS546_INIT_VOUT_COMMAND);

    /* VOUT_OV_WARN_LIMIT */
    smb_read_word(PMBUS_VOUT_OV_WARN_LIMIT, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_OV_WARN_LIMIT: %.2fV", f_value * tps546_config.TPS546_INIT_VOUT_COMMAND);

    /* VOUT_MARGIN_HIGH */
    smb_read_word(PMBUS_VOUT_MARGIN_HIGH, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_MARGIN_HIGH: %.2fV", f_value * tps546_config.TPS546_INIT_VOUT_COMMAND);

    /* --- VOUT_COMMAND --- */
    smb_read_word(PMBUS_VOUT_COMMAND, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_COMMAND: %.2fV", f_value);

    /* VOUT_MARGIN_LOW */
    smb_read_word(PMBUS_VOUT_MARGIN_LOW, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_MARGIN_LOW: %.2fV", f_value * tps546_config.TPS546_INIT_VOUT_COMMAND);

    /* VOUT_UV_WARN_LIMIT */
    smb_read_word(PMBUS_VOUT_UV_WARN_LIMIT, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_UV_WARN_LIMIT: %.2fV", f_value * tps546_config.TPS546_INIT_VOUT_COMMAND);

    /* VOUT_UV_FAULT_LIMIT */
    smb_read_word(PMBUS_VOUT_UV_FAULT_LIMIT, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_UV_FAULT_LIMIT: %.2fV", f_value * tps546_config.TPS546_INIT_VOUT_COMMAND);

    /* VOUT_MIN */
    smb_read_word(PMBUS_VOUT_MIN, &u16_value);
    f_value = ulinear16_2_float(u16_value);
    ESP_LOGI(TAG, "read VOUT_MIN: %.2f V", f_value);
}

