#include "bzm/bzm_test_state.h"
#include "unity.h"

#define I2C_MASTER_H_
#define Thermal_init thermal_test_init
#define Thermal_set_fan_percent thermal_test_set_fan
#define Thermal_get_fan_speed thermal_test_fan_rpm
#define Thermal_get_fan2_speed thermal_test_fan2_rpm
#define Thermal_get_chip_temp thermal_test_temperature
#define Thermal_get_chip_temp2 thermal_test_temperature2
#define BZM_control_set_fan_percent thermal_test_board_set_fan
#define BZM_control_get_fan_rpm thermal_test_board_rpm
#define ASIC_get_temperature thermal_test_native_temperature
#define EMC2101_set_fan_speed thermal_test_emc2101_set_fan
#define EMC2101_get_fan_speed thermal_test_emc2101_rpm
#define EMC2101_get_external_temp thermal_test_emc2101_external
#define EMC2101_get_internal_temp thermal_test_emc2101_internal
#define EMC2103_set_fan_speed thermal_test_emc2103_set_fan
#define EMC2103_get_fan_speed thermal_test_emc2103_rpm
#define EMC2103_get_external_temp thermal_test_emc2103_external
#define EMC2103_get_external_temp2 thermal_test_emc2103_external2
#define EMC2302_set_fan_speed thermal_test_emc2302_set_fan
#define EMC2302_get_fan_speed thermal_test_emc2302_rpm
#define EMC2302_get_fan2_speed thermal_test_emc2302_rpm2
#define TMP1075_read_temperature thermal_test_tmp1075_temperature
#include "main/thermal/bonanza_fan.c"
#include "main/thermal/thermal.c"

static unsigned board_writes, board_reads, native_reads, legacy_writes;
static esp_err_t board_result;
static float requested_percent, native_temperature;

esp_err_t thermal_test_board_set_fan(float percent)
{
    ++board_writes;
    requested_percent = percent;
    return board_result;
}
esp_err_t thermal_test_board_rpm(uint16_t *rpm)
{
    ++board_reads;
    if (board_result == ESP_OK) *rpm = 2400;
    return board_result;
}
float thermal_test_native_temperature(GlobalState *state)
{
    (void)state;
    ++native_reads;
    return native_temperature;
}
esp_err_t thermal_test_emc2101_set_fan(float percent) { ++legacy_writes; requested_percent = percent; return ESP_OK; }
esp_err_t thermal_test_emc2103_set_fan(float percent) { ++legacy_writes; requested_percent = percent; return ESP_OK; }
esp_err_t thermal_test_emc2302_set_fan(float percent) { ++legacy_writes; requested_percent = percent; return ESP_OK; }
uint16_t thermal_test_emc2101_rpm(void) { return 1100; }
uint16_t thermal_test_emc2103_rpm(void) { return 1300; }
uint16_t thermal_test_emc2302_rpm(void) { return 2300; }
uint16_t thermal_test_emc2302_rpm2(void) { return 2301; }
float thermal_test_emc2101_external(void) { return 61; }
float thermal_test_emc2101_internal(void) { return 41; }
float thermal_test_emc2103_external(void) { return 63; }
float thermal_test_emc2103_external2(void) { return 64; }
float thermal_test_tmp1075_temperature(int channel) { return 51 + channel; }

TEST_CASE("Board fan configuration overrides controller flags independently of family ID", "[asic][thermal][config]")
{
    DeviceConfig config = {.family = FAMILY_GAMMA, .fan_driver = &BONANZA_FAN_DRIVER, .EMC2101 = true};
    board_writes = board_reads = legacy_writes = 0;
    board_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, thermal_test_set_fan(&config, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, requested_percent);
    TEST_ASSERT_EQUAL_UINT16(2400, thermal_test_fan_rpm(&config));
    TEST_ASSERT_EQUAL_UINT32(1, board_writes);
    TEST_ASSERT_EQUAL_UINT32(1, board_reads);
    TEST_ASSERT_EQUAL_UINT32(0, legacy_writes);
    board_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, thermal_test_set_fan(&config, 1));
    TEST_ASSERT_EQUAL_UINT16(0, thermal_test_fan_rpm(&config));
    config.family = FAMILY_BONANZA;
    config.fan_driver = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, thermal_test_set_fan(&config, 0.75f));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, requested_percent);
    TEST_ASSERT_EQUAL_UINT16(1100, thermal_test_fan_rpm(&config));
    TEST_ASSERT_EQUAL_UINT32(1, legacy_writes);
    TEST_ASSERT_EQUAL_UINT32(2, board_reads);
}

TEST_CASE("Existing EMC fan controllers and secondary tach readings retain their behavior", "[asic][thermal][config]")
{
    DeviceConfig configs[] = {{.EMC2101 = true}, {.EMC2103 = true}, {.EMC2302 = true}, {0}};
    const uint16_t rpm[] = {1100, 1300, 2300, 0};
    legacy_writes = 0;
    for (unsigned i = 0; i < sizeof(configs) / sizeof(configs[0]); ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, thermal_test_set_fan(&configs[i], 0.6f));
        TEST_ASSERT_EQUAL_UINT16(rpm[i], thermal_test_fan_rpm(&configs[i]));
        TEST_ASSERT_EQUAL_UINT16(i == 2 ? 2301 : 0, thermal_test_fan2_rpm(&configs[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(3, legacy_writes);
    TEST_ASSERT_EQUAL_FLOAT(0.6f, requested_percent);
}

TEST_CASE("Native temperature capability selects the ASIC interface and preserves unavailable readings", "[asic][thermal][config]")
{
    GlobalState state = {0};
    state.DEVICE_CONFIG.family = FAMILY_GAMMA;
    state.DEVICE_CONFIG.family.asic.native_temperature = true;
    state.DEVICE_CONFIG.EMC2103 = true;
    native_reads = 0;
    native_temperature = 72;
    TEST_ASSERT_EQUAL_FLOAT(72, thermal_test_temperature(&state));
    TEST_ASSERT_EQUAL_FLOAT(-1, thermal_test_temperature2(&state));
    native_temperature = -1;
    TEST_ASSERT_EQUAL_FLOAT(-1, thermal_test_temperature(&state));
    TEST_ASSERT_EQUAL_UINT32(2, native_reads);
    state.DEVICE_CONFIG.family = FAMILY_BONANZA;
    state.DEVICE_CONFIG.family.asic.native_temperature = false;
    TEST_ASSERT_EQUAL_FLOAT(-1, thermal_test_temperature(&state));
    TEST_ASSERT_EQUAL_FLOAT(-1, thermal_test_temperature2(&state));
    state.ASIC_initalized = true;
    TEST_ASSERT_EQUAL_FLOAT(63, thermal_test_temperature(&state));
    TEST_ASSERT_EQUAL_FLOAT(64, thermal_test_temperature2(&state));
    state.DEVICE_CONFIG.EMC2103 = false;
    state.DEVICE_CONFIG.EMC2101 = true;
    TEST_ASSERT_EQUAL_FLOAT(61, thermal_test_temperature(&state));
    state.DEVICE_CONFIG.emc_internal_temp = true;
    TEST_ASSERT_EQUAL_FLOAT(41, thermal_test_temperature(&state));
    state.DEVICE_CONFIG.EMC2101 = false;
    state.DEVICE_CONFIG.TMP1075 = true;
    TEST_ASSERT_EQUAL_FLOAT(51, thermal_test_temperature(&state));
    TEST_ASSERT_EQUAL_FLOAT(52, thermal_test_temperature2(&state));
    TEST_ASSERT_EQUAL_UINT32(2, native_reads);
}
