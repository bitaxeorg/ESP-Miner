#include "bzm/bzm_test_state.h"
#include "unity.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void adc_test_delay(TickType_t ticks);

static unsigned unit_inits, channel_inits, calibrations, adc_reads, regulator_inits, regulator_writes;
static float regulator_voltage;

static esp_err_t adc_test_new_unit(const adc_oneshot_unit_init_cfg_t *config, adc_oneshot_unit_handle_t *handle)
{
    TEST_ASSERT_EQUAL(ADC_UNIT_1, config->unit_id);
    ++unit_inits;
    *handle = (void *)1;
    return ESP_OK;
}
static esp_err_t adc_test_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                  const adc_oneshot_chan_cfg_t *config)
{
    TEST_ASSERT_EQUAL_PTR((void *)1, handle);
    TEST_ASSERT_EQUAL(ADC_CHANNEL_1, channel);
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_12, config->atten);
    ++channel_inits;
    return ESP_OK;
}
static esp_err_t adc_test_calibration(const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *handle)
{
    TEST_ASSERT_EQUAL(ADC_CHANNEL_1, config->chan);
    ++calibrations;
    *handle = (void *)2;
    return ESP_OK;
}
static esp_err_t adc_test_read(adc_oneshot_unit_handle_t handle, adc_channel_t channel, int *raw)
{
    TEST_ASSERT_EQUAL_PTR((void *)1, handle);
    TEST_ASSERT_EQUAL(ADC_CHANNEL_1, channel);
    TEST_ASSERT_EQUAL_UINT32(1, channel_inits);
    ++adc_reads;
    *raw = 1234;
    return ESP_OK;
}
static esp_err_t adc_test_voltage(adc_cali_handle_t handle, int raw, int *voltage)
{
    TEST_ASSERT_EQUAL_PTR((void *)2, handle);
    TEST_ASSERT_EQUAL_INT(1234, raw);
    *voltage = 1200;
    return ESP_OK;
}

#define ADC_init adc_test_init
#define ADC_init_vcore adc_test_init_vcore
#define ADC_get_vcore adc_test_get_vcore
#define adc_oneshot_new_unit adc_test_new_unit
#define adc_oneshot_config_channel adc_test_channel
#define adc_cali_create_scheme_curve_fitting adc_test_calibration
#define adc_oneshot_read adc_test_read
#define adc_cali_raw_to_voltage adc_test_voltage
#define TAG adc_tag
#include "main/adc.c"
#undef TAG

#define VCORE_init adc_test_vcore_init
#define VCORE_is_initialized adc_test_vcore_is_initialized
#define VCORE_set_voltage adc_test_vcore_set_voltage
#define VCORE_get_voltage_mv adc_test_vcore_get_voltage
#define VCORE_get_voltage_min_mv adc_test_vcore_min_voltage
#define VCORE_check_fault adc_test_vcore_check_fault
#define VCORE_get_fault_string adc_test_vcore_fault_string
#define VCORE_get_phase_count adc_test_vcore_phase_count
#define DS4432U_init adc_test_ds4432_init
#define DS4432U_set_voltage adc_test_ds4432_voltage
#define INA260_init adc_test_ina260_init
#define TPS546_init adc_test_tps546_init
#define TPS546_set_vout adc_test_tps546_set_voltage
#define TPS546_get_vout adc_test_tps546_voltage
#define nvs_config_has_key adc_test_has_key
#define nvs_config_get_u16 adc_test_get_u16
#define nvs_config_get_float adc_test_get_float
#define gpio_config adc_test_gpio_config
#define gpio_get_level adc_test_gpio_level
#define gpio_set_level adc_test_gpio_set
#define vTaskDelay adc_test_delay
#define I2C_MASTER_H_
#define CONFIG_GPIO_ASIC_ENABLE 10
#define CONFIG_GPIO_PLUG_SENSE 12
#define TAG vcore_tag
#include "main/power/vcore.c"
#undef TAG

static esp_err_t adc_test_board_init(GlobalState *state, TPS546_CONFIG config)
{ (void)state; (void)config; ++regulator_inits; return ESP_OK; }
static esp_err_t adc_test_board_voltage(GlobalState *state, float volts)
{ (void)state; regulator_voltage = volts; return ESP_OK; }
esp_err_t adc_test_ds4432_init(void) { ++regulator_inits; return ESP_OK; }
esp_err_t adc_test_ds4432_voltage(float volts) { ++regulator_writes; regulator_voltage = volts; return ESP_OK; }
esp_err_t adc_test_ina260_init(void) { return ESP_OK; }
esp_err_t adc_test_tps546_init(TPS546_CONFIG config) { (void)config; ++regulator_inits; return ESP_OK; }
esp_err_t adc_test_tps546_set_voltage(float volts) { ++regulator_writes; regulator_voltage = volts; return ESP_OK; }
float adc_test_tps546_voltage(void) { return regulator_voltage; }
bool adc_test_has_key(NvsConfigKey key) { (void)key; return false; }
uint16_t adc_test_get_u16(NvsConfigKey key) { (void)key; return 0; }
float adc_test_get_float(NvsConfigKey key) { (void)key; return 0; }
esp_err_t adc_test_gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
int adc_test_gpio_level(gpio_num_t pin) { (void)pin; return 1; }
esp_err_t adc_test_gpio_set(gpio_num_t pin, uint32_t level) { (void)pin; (void)level; return ESP_OK; }
void adc_test_delay(TickType_t ticks) { (void)ticks; }

static void fixture(void)
{
    unit_inits = channel_inits = calibrations = adc_reads = regulator_inits = regulator_writes = 0;
    adc1_handle = NULL;
    adc1_cali_chan1_handle = NULL;
    vcore_channel_initialized = false;
    adc_test_init();
    TEST_ASSERT_EQUAL_UINT32(1, unit_inits);
    TEST_ASSERT_EQUAL_UINT32(0, channel_inits);
    TEST_ASSERT_EQUAL_UINT32(0, calibrations);
}

TEST_CASE("PMBus boards leave GPIO2 unclaimed by ADC through startup and voltage reads", "[asic][power][adc]")
{
    const VcoreDriver driver = {.init = adc_test_board_init, .set_voltage = adc_test_board_voltage};
    const FamilyConfig families[] = {FAMILY_BONANZA, FAMILY_GAMMA, FAMILY_HEX};
    for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); ++i) {
        fixture();
        GlobalState state = {0};
        state.DEVICE_CONFIG.family = families[i];
        if (i == 0) state.DEVICE_CONFIG.vcore_driver = &driver;
        state.DEVICE_CONFIG.TPS546 = true;
        regulator_voltage = 1.2f * families[i].voltage_domains;
        TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_init(&state));
        TEST_ASSERT_TRUE(adc_test_vcore_is_initialized());
        TEST_ASSERT_EQUAL_UINT32(1, regulator_inits);
        TEST_ASSERT_INT_WITHIN(1, 1200, adc_test_vcore_get_voltage(&state));
        TEST_ASSERT_EQUAL_UINT32(0, channel_inits);
        TEST_ASSERT_EQUAL_UINT32(0, calibrations);
        TEST_ASSERT_EQUAL_UINT32(0, adc_reads);
    }
}

TEST_CASE("ADC boards configure and calibrate Vcore before voltage reads", "[asic][power][adc]")
{
    fixture();
    GlobalState state = {0};
    state.DEVICE_CONFIG.family = FAMILY_ULTRA;
    state.DEVICE_CONFIG.DS4432U = state.DEVICE_CONFIG.INA260 = true;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_init(&state));
        TEST_ASSERT_TRUE(adc_test_vcore_is_initialized());
        TEST_ASSERT_EQUAL_UINT32(1, channel_inits);
        TEST_ASSERT_EQUAL_UINT32(1, calibrations);
        TEST_ASSERT_EQUAL_INT(1200, adc_test_vcore_get_voltage(&state));
    }
    TEST_ASSERT_EQUAL_UINT32(2, adc_reads);
}

static esp_err_t board_result;
static unsigned board_inits, board_voltage_writes;
static esp_err_t board_init(GlobalState *state, TPS546_CONFIG config)
{
    TEST_ASSERT_EQUAL_FLOAT(state->DEVICE_CONFIG.family.tps546_config->TPS546_INIT_VOUT_COMMAND,
                            config.TPS546_INIT_VOUT_COMMAND);
    ++board_inits;
    return board_result;
}
static esp_err_t board_set_voltage(GlobalState *state, float volts)
{
    (void)state;
    ++board_voltage_writes;
    regulator_voltage = volts;
    return board_result;
}

TEST_CASE("Configured Vcore driver owns board sequencing and propagates failures independently of family ID", "[asic][power][config]")
{
    fixture();
    const VcoreDriver driver = {.init = board_init, .set_voltage = board_set_voltage};
    GlobalState state = {0};
    state.DEVICE_CONFIG.family = FAMILY_GAMMA;
    state.DEVICE_CONFIG.family.tps546_config = &TPS546_CONFIG_BONANZA_1004;
    state.DEVICE_CONFIG.vcore_driver = &driver;
    board_inits = board_voltage_writes = 0;
    board_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, adc_test_vcore_init(&state));
    TEST_ASSERT_FALSE(adc_test_vcore_is_initialized());
    TEST_ASSERT_EQUAL(ESP_FAIL, adc_test_vcore_set_voltage(&state, 2.8f));
    board_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_init(&state));
    TEST_ASSERT_TRUE(adc_test_vcore_is_initialized());
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_set_voltage(&state, 2.95f));
    TEST_ASSERT_EQUAL_FLOAT(2.95f, regulator_voltage);
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_set_voltage(&state, 0));
    TEST_ASSERT_EQUAL_FLOAT(0, regulator_voltage);
    TEST_ASSERT_EQUAL_UINT32(2, board_inits);
    TEST_ASSERT_EQUAL_UINT32(3, board_voltage_writes);
    TEST_ASSERT_EQUAL_UINT32(0, regulator_inits);
    TEST_ASSERT_EQUAL_UINT32(0, regulator_writes);
    TEST_ASSERT_EQUAL_UINT32(0, channel_inits);
}

TEST_CASE("Boards without a Vcore override retain regulator selection and voltage-domain scaling", "[asic][power][config]")
{
    fixture();
    GlobalState state = {0};
    state.DEVICE_CONFIG.family = FAMILY_ULTRA;
    state.DEVICE_CONFIG.DS4432U = true;
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_set_voltage(&state, 1.2f));
    TEST_ASSERT_EQUAL_FLOAT(1.2f, regulator_voltage);
    TEST_ASSERT_EQUAL_UINT32(1, regulator_writes);
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_set_voltage(&state, 0));
    TEST_ASSERT_EQUAL_UINT32(1, regulator_writes);
    state.DEVICE_CONFIG.family = FAMILY_GAMMA_DUO;
    state.DEVICE_CONFIG.DS4432U = false;
    state.DEVICE_CONFIG.TPS546 = true;
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_set_voltage(&state, 1.2f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2f * FAMILY_GAMMA_DUO.voltage_domains, regulator_voltage);
    TEST_ASSERT_EQUAL(ESP_OK, adc_test_vcore_set_voltage(&state, 0));
    TEST_ASSERT_EQUAL_FLOAT(0, regulator_voltage);
    TEST_ASSERT_EQUAL_UINT32(3, regulator_writes);
}
