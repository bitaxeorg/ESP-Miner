#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

/* Compile the production regulator with a private PMBus and application view. */
#define TEST_STUB_GLOBAL_STATE_H
typedef struct { uint8_t power_fault; } SystemModule;
typedef struct GlobalState { SystemModule SYSTEM_MODULE; } GlobalState;
#define I2C_MASTER_H_
typedef void *i2c_master_dev_handle_t;

static uint8_t registers[256][8];
static unsigned reads[256], write_count, fail_write_at;
static int fail_read;
static TickType_t ticks;

static esp_err_t tps_read(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *data, size_t length)
{
    (void)device;
    ++reads[reg];
    if (reg == fail_read) return ESP_FAIL;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(registers[reg]), length);
    memcpy(data, registers[reg], length);
    return ESP_OK;
}
static esp_err_t tps_write(uint8_t reg, const uint8_t *data, size_t length)
{
    if (++write_count == fail_write_at) return ESP_FAIL;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(registers[reg]), length);
    if (length) memcpy(registers[reg], data, length);
    return ESP_OK;
}
static esp_err_t tps_write_byte(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{ (void)device; return tps_write(reg, &value, 1); }
static esp_err_t tps_write_word(i2c_master_dev_handle_t device, uint8_t reg, uint16_t value)
{
    (void)device;
    uint8_t bytes[] = {value & 0xff, value >> 8};
    return tps_write(reg, bytes, sizeof(bytes));
}
static esp_err_t tps_write_bytes(i2c_master_dev_handle_t device, uint8_t *data, uint8_t length)
{ (void)device; return tps_write(data[0], &data[1], length - 1); }
static esp_err_t tps_write_addr(i2c_master_dev_handle_t device, uint8_t reg)
{ (void)device; return tps_write(reg, NULL, 0); }
static esp_err_t tps_add_device(uint8_t address, i2c_master_dev_handle_t *device, const char *tag)
{ (void)address; (void)tag; *device = (void *)1; return ESP_OK; }
static esp_err_t tps_receive(i2c_master_dev_handle_t device, uint8_t *data, size_t length, int timeout)
{ (void)device; (void)data; (void)length; (void)timeout; return ESP_ERR_TIMEOUT; }
static void tps_delay(TickType_t delay) { ticks += delay; }
static TickType_t tps_ticks(void) { return ticks; }

#define i2c_bitaxe_register_read tps_read
#define i2c_bitaxe_register_write_byte tps_write_byte
#define i2c_bitaxe_register_write_word tps_write_word
#define i2c_bitaxe_register_write_bytes tps_write_bytes
#define i2c_bitaxe_register_write_addr tps_write_addr
#define i2c_bitaxe_add_device tps_add_device
#define i2c_master_receive tps_receive
#define vTaskDelay tps_delay
#define xTaskGetTickCount tps_ticks
#include "../../../main/power/bonanza_tps546.c"

static void reset_pmbus(void)
{
    memset(registers, 0, sizeof(registers));
    memset(reads, 0, sizeof(reads));
    write_count = fail_write_at = ticks = 0;
    fail_read = -1;
    tps546_config_written = false;
    tps546_power_good_grace_until = 0;
    registers[PMBUS_VOUT_MODE][0] = 0x17; /* ULINEAR16, exponent -9. */
    const uint8_t device_id[] = {6, 0x54, 0x49, 0x54, 0x6d, 0x24, 0x41};
    memcpy(registers[PMBUS_IC_DEVICE_ID], device_id, sizeof(device_id));
    esp_log_level_set("TPS546", ESP_LOG_NONE);
}
static uint16_t word(uint8_t reg)
{ return registers[reg][0] | ((uint16_t)registers[reg][1] << 8); }
static void set_word(uint8_t reg, uint16_t value)
{
    registers[reg][0] = value & 0xff;
    registers[reg][1] = value >> 8;
}

TEST_CASE("Bonanza regulator initializes its board profile with output off", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_init());
    TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
    TEST_ASSERT_EQUAL_HEX8(0x1b, registers[PMBUS_ON_OFF_CONFIG][0]);
    TEST_ASSERT_EQUAL_HEX8(0xff, registers[PMBUS_PHASE][0]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, word(PMBUS_STACK_CONFIG));
    TEST_ASSERT_EQUAL_HEX16(0x0599, word(PMBUS_VOUT_COMMAND));
    TEST_ASSERT_EQUAL_HEX16(0x0433, word(PMBUS_VOUT_MIN));
    TEST_ASSERT_EQUAL_HEX16(0x0666, word(PMBUS_VOUT_MAX));
    char detail[80];
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_check_protection(detail, sizeof(detail)));
    registers[PMBUS_VOUT_OV_FAULT_RESPONSE][0] ^= 1;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, BONANZA_TPS546_check_protection(detail, sizeof(detail)));
    TEST_ASSERT_NOT_NULL(strstr(detail, "VOUT_OV_FAULT_RESPONSE"));
}

TEST_CASE("Bonanza regulator stops at every failed profile write", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, write_config());
    unsigned total_writes = write_count;
    TEST_ASSERT_GREATER_THAN_UINT32(40, total_writes);
    for (unsigned failure = 1; failure <= total_writes; ++failure) {
        reset_pmbus();
        fail_write_at = failure;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, write_config());
        TEST_ASSERT_EQUAL_UINT32(failure, write_count);
        char detail[80];
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, BONANZA_TPS546_check_protection(detail, sizeof(detail)));
        TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
    }
}

TEST_CASE("Bonanza voltage changes preserve the fixed protection thresholds", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_init());
    const uint8_t limits[] = {PMBUS_VOUT_OV_FAULT_LIMIT, PMBUS_VOUT_UV_FAULT_LIMIT,
                             PMBUS_VOUT_OV_WARN_LIMIT, PMBUS_VOUT_UV_WARN_LIMIT};
    uint16_t before[4];
    for (size_t i = 0; i < 4; ++i) before[i] = word(limits[i]);
    unsigned previous_writes = write_count;
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_set_vout(3.0f));
    TEST_ASSERT_EQUAL_UINT32(previous_writes + 2, write_count);
    TEST_ASSERT_EQUAL_HEX16(0x0600, word(PMBUS_VOUT_COMMAND));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_ON, registers[PMBUS_OPERATION][0]);
    for (size_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_HEX16(before[i], word(limits[i]));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, BONANZA_TPS546_set_vout(3.3f));
    TEST_ASSERT_EQUAL_UINT32(previous_writes + 2, write_count);
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_set_vout(0));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
}

TEST_CASE("Bonanza live snapshots read only operating data and reject read failures", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_init());
    memset(reads, 0, sizeof(reads));
    set_word(PMBUS_READ_VOUT, 0x0599);
    set_word(PMBUS_READ_VIN, 12);
    set_word(PMBUS_READ_IOUT, 15);
    set_word(PMBUS_READ_TEMPERATURE_1, 60);
    BONANZA_TPS546_StatusSnapshot snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_snapshot_status(&snapshot));
    TEST_ASSERT_TRUE(snapshot.vout_command_matches_active_config);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 2.798828125f, snapshot.read_vout);
    TEST_ASSERT_EQUAL_FLOAT(12, snapshot.read_vin);
    TEST_ASSERT_EQUAL_FLOAT(15, snapshot.read_iout);
    TEST_ASSERT_EQUAL_INT(60, snapshot.read_temp1);
    const uint8_t live[] = {PMBUS_STATUS_WORD, PMBUS_OPERATION, PMBUS_VOUT_MODE, PMBUS_VOUT_COMMAND,
                           PMBUS_READ_VOUT, PMBUS_READ_VIN, PMBUS_READ_IOUT, PMBUS_READ_TEMPERATURE_1};
    unsigned total_reads = 0;
    for (size_t i = 0; i < 256; ++i) total_reads += reads[i];
    TEST_ASSERT_EQUAL_UINT32(sizeof(live), total_reads);
    for (size_t i = 0; i < sizeof(live); ++i) {
        TEST_ASSERT_EQUAL_UINT32(1, reads[live[i]]);
        fail_read = live[i];
        TEST_ASSERT_NOT_EQUAL(ESP_OK, BONANZA_TPS546_snapshot_status(&snapshot));
        fail_read = -1;
    }
    set_word(PMBUS_VOUT_COMMAND, 0x0600);
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_snapshot_status(&snapshot));
    TEST_ASSERT_FALSE(snapshot.vout_command_matches_active_config);
    TEST_ASSERT_EQUAL_FLOAT(3, snapshot.vout_command);
}

TEST_CASE("Bonanza fault decoding preserves enable grace and fault reporting", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_init());
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_set_vout(2.8f));
    GlobalState state = {0};
    set_word(PMBUS_STATUS_WORD, BONANZA_TPS546_STATUS_OFF);
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(0, state.SYSTEM_MODULE.power_fault);
    set_word(PMBUS_STATUS_WORD, BONANZA_TPS546_STATUS_TEMP);
    registers[PMBUS_STATUS_TEMPERATURE][0] = BONANZA_TPS546_STATUS_TEMP_OTF;
    fail_read = PMBUS_STATUS_TEMPERATURE;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, BONANZA_TPS546_check_status(&state));
    fail_read = -1;
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(1, state.SYSTEM_MODULE.power_fault);
    set_word(PMBUS_STATUS_WORD, 0);
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(0, state.SYSTEM_MODULE.power_fault);
    ticks += pdMS_TO_TICKS(BONANZA_TPS546_POWER_GOOD_GRACE_MS);
    set_word(PMBUS_STATUS_WORD, BONANZA_TPS546_STATUS_OFF);
    TEST_ASSERT_EQUAL(ESP_OK, BONANZA_TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(1, state.SYSTEM_MODULE.power_fault);
}
