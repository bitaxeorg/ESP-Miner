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
static uint8_t first_byte_written[256];
static unsigned writes[256], write_count, fail_write_at;
static int fail_read, ignored_write;
static TickType_t ticks;

static esp_err_t tps_read(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *data, size_t length)
{
    (void)device;
    if (reg == fail_read) return ESP_FAIL;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(registers[reg]), length);
    memcpy(data, registers[reg], length);
    return ESP_OK;
}
static esp_err_t tps_write(uint8_t reg, const uint8_t *data, size_t length)
{
    if (writes[reg] == 0 && length == 1) first_byte_written[reg] = data[0];
    ++writes[reg];
    if (++write_count == fail_write_at) return ESP_FAIL;
    if (reg == ignored_write) return ESP_OK; /* PMBus may ACK a rejected write. */
    if (reg == 0x21 && length == 2) {
        uint16_t command = data[0] | ((uint16_t)data[1] << 8);
        uint16_t minimum = registers[0x2b][0] | ((uint16_t)registers[0x2b][1] << 8);
        uint16_t maximum = registers[0x24][0] | ((uint16_t)registers[0x24][1] << 8);
        /* Reject commands that would clamp to the D24S's current bounds. */
        if (command < minimum || command > maximum) return ESP_ERR_INVALID_ARG;
    }
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
#include "../../../main/power/TPS546.c"

static void reset_pmbus(void)
{
    memset(registers, 0, sizeof(registers));
    memset(first_byte_written, 0, sizeof(first_byte_written));
    memset(writes, 0, sizeof(writes));
    write_count = fail_write_at = ticks = 0;
    fail_read = ignored_write = -1;
    tps546_config = TPS546_CONFIG_BONANZA_1004;
    tps546_power_good_grace_until = 0;
    registers[PMBUS_VOUT_MODE][0] = 0x17; /* ULINEAR16, exponent -9. */
    registers[PMBUS_INTERLEAVE][0] = 0x10;
    registers[PMBUS_VOUT_OV_FAULT_RESPONSE][0] = 0xbd;
    registers[PMBUS_VOUT_UV_FAULT_RESPONSE][0] = 0xbe;
    registers[PMBUS_VOUT_COMMAND][1] = 2; /* Attached 1004 defaults: 1.0 V. */
    registers[PMBUS_VOUT_MAX][1] = 3;     /* 1.5 V. */
    registers[PMBUS_VOUT_MIN][1] = 1;     /* 0.5 V. */
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
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
    TEST_ASSERT_EQUAL_HEX8(0x1f, registers[PMBUS_ON_OFF_CONFIG][0]);
    TEST_ASSERT_EQUAL_HEX8(0x97, registers[PMBUS_VOUT_MODE][0]);
    TEST_ASSERT_EQUAL_HEX8(0xff, registers[PMBUS_PHASE][0]);
    TEST_ASSERT_EQUAL_HEX8(0x10, registers[PMBUS_SYNC_CONFIG][0]);
    TEST_ASSERT_EQUAL_HEX16(0x0010, word(PMBUS_INTERLEAVE));
    TEST_ASSERT_EQUAL_HEX16(0x0000, word(PMBUS_STACK_CONFIG));
    TEST_ASSERT_EQUAL_HEX16(0x0599, word(PMBUS_VOUT_COMMAND));
    TEST_ASSERT_EQUAL_HEX16(0x0433, word(PMBUS_VOUT_MIN));
    TEST_ASSERT_EQUAL_HEX16(0x0666, word(PMBUS_VOUT_MAX));
    TEST_ASSERT_EQUAL_INT(325, TPS546_get_frequency());
    TEST_ASSERT_EQUAL_HEX16(INIT_PIN_DETECT_OVERRIDE, word(PMBUS_PIN_DETECT_OVERRIDE));
    TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_INTERLEAVE]);
    TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_OV_FAULT_RESPONSE]);
    TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_UV_FAULT_RESPONSE]);
}

TEST_CASE("TPS546 initial and full configuration honor enable-pin control", "[asic][tps546]")
{
    for (unsigned use_enable_pin = 0; use_enable_pin <= 1; ++use_enable_pin) {
        reset_pmbus();
        TPS546_CONFIG config = TPS546_CONFIG_BONANZA_1004;
        config.TPS546_USE_ENABLE_PIN = use_enable_pin;
        const uint8_t expected = use_enable_pin ? 0x1f : 0x1b;
        TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(config));
        TEST_ASSERT_EQUAL_UINT32(2, writes[PMBUS_ON_OFF_CONFIG]);
        TEST_ASSERT_EQUAL_HEX8(expected, first_byte_written[PMBUS_ON_OFF_CONFIG]);
        TEST_ASSERT_EQUAL_HEX8(expected, registers[PMBUS_ON_OFF_CONFIG][0]);
        TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
    }
}

TEST_CASE("Bonanza 1004 programs Anvil compensation on D24A and D24S", "[asic][bonanza][tps546]")
{
    const uint8_t legacy[] = {5, 0x13, 0x11, 0x8c, 0x1d, 0x06};
    const uint8_t anvil[] = {5, 0x13, 0x11, 0x8c, 0x1d, 0x08};
    const uint8_t device_ids[] = {0x41, 0x62};
    for (size_t i = 0; i < sizeof(device_ids); ++i) {
        reset_pmbus();
        registers[PMBUS_IC_DEVICE_ID][6] = device_ids[i];
        memcpy(registers[PMBUS_COMPENSATION_CONFIG], legacy, sizeof(legacy));
        TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(anvil, registers[PMBUS_COMPENSATION_CONFIG], sizeof(anvil));
        TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
    }
}

TEST_CASE("Existing TPS546 profiles keep their voltage, phase and enable behavior", "[asic][tps546]")
{
    const TPS546_CONFIG *profiles[] = {
        &TPS546_CONFIG_DEFAULT, &TPS546_CONFIG_HEX, &TPS546_CONFIG_GAMMA_TURBO,
        &TPS546_CONFIG_NAJA_DUO, &TPS546_CONFIG_GAMMA_HEX,
    };
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        reset_pmbus();
        const TPS546_CONFIG *profile = profiles[i];
        registers[PMBUS_VOUT_MODE][0] = 0x97;
        set_word(PMBUS_INTERLEAVE, 0x0020);
        TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(*profile));
        TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
        TEST_ASSERT_EQUAL_HEX8(0x1b, registers[PMBUS_ON_OFF_CONFIG][0]);
        TEST_ASSERT_EQUAL_HEX8(profile->TPS546_INIT_SYNC_CONFIG, registers[PMBUS_SYNC_CONFIG][0]);
        TEST_ASSERT_EQUAL_HEX16(profile->TPS546_INIT_STACK_CONFIG, word(PMBUS_STACK_CONFIG));
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(profile->TPS546_INIT_VOUT_COMMAND * 512.0f), word(PMBUS_VOUT_COMMAND));
        TEST_ASSERT_EQUAL_HEX8(profile->TPS546_INIT_PHASE, registers[PMBUS_PHASE][0]);
        TEST_ASSERT_EQUAL_HEX16(INIT_PIN_DETECT_OVERRIDE, word(PMBUS_PIN_DETECT_OVERRIDE));
        TEST_ASSERT_EQUAL_HEX16(0x0020, word(PMBUS_INTERLEAVE));
        TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_MODE]);
        TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_INTERLEAVE]);
        TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(profile->TPS546_INIT_VOUT_COMMAND));
        TEST_ASSERT_EQUAL_HEX8(OPERATION_ON, registers[PMBUS_OPERATION][0]);
        TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(0));
    }
}

TEST_CASE("Bonanza voltage changes preserve the relative protection thresholds", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    const uint8_t limits[] = {PMBUS_VOUT_OV_FAULT_LIMIT, PMBUS_VOUT_UV_FAULT_LIMIT,
                             PMBUS_VOUT_OV_WARN_LIMIT, PMBUS_VOUT_UV_WARN_LIMIT};
    uint16_t before[4];
    for (size_t i = 0; i < 4; ++i) before[i] = word(limits[i]);
    unsigned previous_writes = write_count;
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(3.0f));
    TEST_ASSERT_EQUAL_UINT32(previous_writes + 8, write_count);
    TEST_ASSERT_EQUAL_HEX16(0x0600, word(PMBUS_VOUT_COMMAND));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_ON, registers[PMBUS_OPERATION][0]);
    for (size_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_HEX16(before[i], word(limits[i]));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, TPS546_set_vout(3.3f));
    TEST_ASSERT_EQUAL_UINT32(previous_writes + 8, write_count);
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(0));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
}

TEST_CASE("Bonanza D24S uses relative limits and preserves the voltage exponent", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    registers[PMBUS_IC_DEVICE_ID][6] = 0x62;
    registers[PMBUS_VOUT_MODE][0] = 0x96; /* Relative, exponent -10. */
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    TEST_ASSERT_EQUAL_HEX8(0x96, registers[PMBUS_VOUT_MODE][0]);
    TEST_ASSERT_EQUAL_HEX8(0x1f, registers[PMBUS_ON_OFF_CONFIG][0]);
    TEST_ASSERT_EQUAL_HEX16(0x0b33, word(PMBUS_VOUT_COMMAND));
    TEST_ASSERT_EQUAL_HEX16(0x0500, word(PMBUS_VOUT_OV_FAULT_LIMIT)); /* 125%, not 1.25 V. */
    TEST_ASSERT_EQUAL_HEX16(0x0300, word(PMBUS_VOUT_UV_FAULT_LIMIT)); /* 75%. */
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(3.0f));
    TEST_ASSERT_EQUAL_HEX16(0x0c00, word(PMBUS_VOUT_COMMAND));
    TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_MODE]);
}

TEST_CASE("Bonanza relative-mode setup requires successful disable and mode writes", "[asic][bonanza][tps546]")
{
    for (unsigned failure = 1; failure <= 3; ++failure) {
        reset_pmbus();
        fail_write_at = failure;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
        TEST_ASSERT_EQUAL_UINT32(failure, write_count);
        TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_COMMAND]);
    }
    reset_pmbus();
    fail_read = PMBUS_VOUT_MODE;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_COMMAND]);
    reset_pmbus();
    ignored_write = PMBUS_VOUT_MODE;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_OFF, registers[PMBUS_OPERATION][0]);
    TEST_ASSERT_EQUAL_UINT32(0, writes[PMBUS_VOUT_COMMAND]);
}

TEST_CASE("Bonanza power validation can use the existing regulator status snapshot", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(2.8f));
    set_word(PMBUS_READ_VOUT, 0x0599);
    set_word(PMBUS_READ_VIN, 12);
    set_word(PMBUS_READ_IOUT, 15);
    set_word(PMBUS_READ_TEMPERATURE_1, 60);
    TPS546_StatusSnapshot snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_snapshot_status(&snapshot));
    TEST_ASSERT_EQUAL_HEX8(OPERATION_ON, snapshot.operation);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 2.798828125f, snapshot.read_vout);
    TEST_ASSERT_EQUAL_FLOAT(12, snapshot.read_vin);
    TEST_ASSERT_EQUAL_FLOAT(15, snapshot.read_iout);
    TEST_ASSERT_EQUAL_INT(60, snapshot.read_temp1);
    const uint8_t required[] = {PMBUS_STATUS_WORD, PMBUS_OPERATION, PMBUS_VOUT_COMMAND,
                               PMBUS_READ_VOUT, PMBUS_READ_VIN, PMBUS_READ_IOUT, PMBUS_READ_TEMPERATURE_1};
    for (size_t i = 0; i < sizeof(required); ++i) {
        fail_read = required[i];
        TEST_ASSERT_NOT_EQUAL(ESP_OK, TPS546_snapshot_status(&snapshot));
        fail_read = -1;
    }
    set_word(PMBUS_VOUT_COMMAND, 0x0600);
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_snapshot_status(&snapshot));
    TEST_ASSERT_EQUAL_FLOAT(3, snapshot.vout_command);
}

TEST_CASE("Bonanza fault decoding preserves enable grace and fault reporting", "[asic][bonanza][tps546]")
{
    reset_pmbus();
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_init(TPS546_CONFIG_BONANZA_1004));
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_set_vout(2.8f));
    GlobalState state = {0};
    set_word(PMBUS_STATUS_WORD, TPS546_STATUS_OFF);
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(0, state.SYSTEM_MODULE.power_fault);
    set_word(PMBUS_STATUS_WORD, TPS546_STATUS_TEMP);
    registers[PMBUS_STATUS_TEMPERATURE][0] = TPS546_STATUS_TEMP_OTF;
    fail_read = PMBUS_STATUS_TEMPERATURE;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, TPS546_check_status(&state));
    fail_read = -1;
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(1, state.SYSTEM_MODULE.power_fault);
    set_word(PMBUS_STATUS_WORD, 0);
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(0, state.SYSTEM_MODULE.power_fault);
    ticks += pdMS_TO_TICKS(TPS546_POWER_GOOD_GRACE_MS);
    set_word(PMBUS_STATUS_WORD, TPS546_STATUS_OFF);
    TEST_ASSERT_EQUAL(ESP_OK, TPS546_check_status(&state));
    TEST_ASSERT_EQUAL_UINT8(1, state.SYSTEM_MODULE.power_fault);
}
