#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"

#include "DS4432U.h"
#include "thermal.h"
#include "vcore.h"
#include "power.h"
#include "nvs_config.h"
#include "global_state.h"
#include "asic_reset.h"
#include "device_config.h"
#include "hashrate_monitor_task.h"
#include "PID.h"
#include "self_test.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE

/////Test Constants/////
// Test Fan Speed
#define FAN_SPEED_TARGET_MIN 1000 // RPM
#define SELF_TEST_MIN_FAN_PERCENT 10.0f
#define SELF_TEST_MAX_FAN_PERCENT 100.0f
#define SELF_TEST_PID_SAMPLE_TIME_MS 100
#define SELF_TEST_PID_P 5.0f
#define SELF_TEST_PID_I 0.1f
#define SELF_TEST_PID_D 2.0f
#define SELF_TEST_DOMAIN_HASHRATE_TOLERANCE 0.33f
#define SELF_TEST_DOMAIN_REJECTED_WARN_RATIO 0.25f

#define SELF_TEST_CORE_VOLTAGE_TOLERANCE 0.10f

// Test Power Consumption
#define POWER_CONSUMPTION_MARGIN 3 //+/- watts

// Test Input Voltage
#define INPUT_VOLTAGE_MARGIN 0.10f // +/- 10%

// Test Difficulty
#define DIFFICULTY 16

static const char * TAG = "self_test";

static SemaphoreHandle_t longPressSemaphore;
static bool isFactoryTest = false;

// local function prototypes
static void tests_done(GlobalState * GLOBAL_STATE, bool test_result);

typedef struct {
    float hashrate_sum;
    uint32_t sample_count;
    uint32_t rejected_sample_count;
    uint64_t last_sample_time_us;
} SelfTestDomainAverage;

typedef struct {
    int asic_count;
    int hash_domains;
    SelfTestDomainAverage *domains;
} SelfTestDomainAverages;

typedef enum {
    SELF_TEST_DOMAIN_OK,
    SELF_TEST_DOMAIN_FAIL,
    SELF_TEST_DOMAIN_UNRELIABLE,
} SelfTestDomainStatus;

static size_t self_test_domain_index(const SelfTestDomainAverages * averages, int asic_nr, int domain_nr)
{
    return (size_t)asic_nr * averages->hash_domains + domain_nr;
}

static SelfTestDomainAverage * self_test_domain_get(SelfTestDomainAverages * averages, int asic_nr, int domain_nr)
{
    return &averages->domains[self_test_domain_index(averages, asic_nr, domain_nr)];
}

static esp_err_t self_test_domain_averages_init(SelfTestDomainAverages * averages, int asic_count, int hash_domains)
{
    memset(averages, 0, sizeof(*averages));
    averages->asic_count = asic_count;
    averages->hash_domains = hash_domains;

    size_t domain_count = (size_t)asic_count * hash_domains;
    averages->domains = calloc(domain_count, sizeof(*averages->domains));
    if (!averages->domains) {
        memset(averages, 0, sizeof(*averages));
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void self_test_domain_averages_free(SelfTestDomainAverages * averages)
{
    free(averages->domains);
    memset(averages, 0, sizeof(*averages));
}

static void self_test_domain_averages_prime(GlobalState * GLOBAL_STATE, SelfTestDomainAverages * averages)
{
    HashrateMonitorModule * monitor = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    if (!monitor->is_initialized || !averages->domains) {
        return;
    }

    pthread_mutex_lock(&monitor->lock);
    for (int asic_nr = 0; asic_nr < averages->asic_count; asic_nr++) {
        for (int domain_nr = 0; domain_nr < averages->hash_domains; domain_nr++) {
            SelfTestDomainAverage * average = self_test_domain_get(averages, asic_nr, domain_nr);
            average->last_sample_time_us = monitor->domain_measurements[asic_nr][domain_nr].time_us;
        }
    }
    pthread_mutex_unlock(&monitor->lock);
}

static void self_test_domain_averages_sample(GlobalState * GLOBAL_STATE,
                                             SelfTestDomainAverages * averages,
                                             float expected_domain_hashrate)
{
    HashrateMonitorModule * monitor = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    if (!monitor->is_initialized || !averages->domains) {
        return;
    }

    float max_plausible_hashrate = expected_domain_hashrate * 3.0f;

    pthread_mutex_lock(&monitor->lock);
    for (int asic_nr = 0; asic_nr < averages->asic_count; asic_nr++) {
        for (int domain_nr = 0; domain_nr < averages->hash_domains; domain_nr++) {
            measurement_t measurement = monitor->domain_measurements[asic_nr][domain_nr];
            SelfTestDomainAverage * average = self_test_domain_get(averages, asic_nr, domain_nr);

            if (measurement.time_us == 0 || measurement.time_us == average->last_sample_time_us) {
                continue;
            }

            if (average->last_sample_time_us == 0) {
                average->last_sample_time_us = measurement.time_us;
                continue;
            }
            average->last_sample_time_us = measurement.time_us;

            bool rejected = !isfinite(measurement.hashrate) || measurement.hashrate > max_plausible_hashrate;

            if (rejected) {
                average->rejected_sample_count++;
                continue;
            }

            average->hashrate_sum += measurement.hashrate;
            average->sample_count++;
        }
    }
    pthread_mutex_unlock(&monitor->lock);
}

static const SelfTestDomainAverage * self_test_domain_get_const(const SelfTestDomainAverages * averages, int asic_nr, int domain_nr)
{
    return &averages->domains[self_test_domain_index(averages, asic_nr, domain_nr)];
}

static float self_test_domain_average_hashrate(const SelfTestDomainAverage * average)
{
    return average->sample_count > 0 ? average->hashrate_sum / average->sample_count : 0.0f;
}

static void self_test_set_fan_percent(GlobalState * GLOBAL_STATE, float fan_percent)
{
    if (fan_percent > SELF_TEST_MAX_FAN_PERCENT) fan_percent = SELF_TEST_MAX_FAN_PERCENT;
    if (fan_percent < 0.0f) fan_percent = 0.0f;

    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc = fan_percent;
    if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, fan_percent / 100.0f) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set fan speed to %.1f%%", fan_percent);
        self_test_show_message(GLOBAL_STATE, "FAN:FAIL");
        tests_done(GLOBAL_STATE, false);
    }
}

static bool self_test_temp_invalid(float temp)
{
    return !isfinite(temp) || temp == -1.0f || temp == 127.0f;
}

static float self_test_get_control_temp(GlobalState * GLOBAL_STATE)
{
    float temp = Thermal_get_chip_temp(GLOBAL_STATE);
    float temp2 = Thermal_get_chip_temp2(GLOBAL_STATE);

    if (self_test_temp_invalid(temp)) {
        return temp2;
    }

    if (!self_test_temp_invalid(temp2) && temp2 > temp) {
        return temp2;
    }

    return temp;
}

static float self_test_get_valid_control_temp(GlobalState * GLOBAL_STATE)
{
    float temp = self_test_get_control_temp(GLOBAL_STATE);
    if (self_test_temp_invalid(temp)) {
        ESP_LOGE(TAG, "Open circuit or no result on temperature sensor: %.1f°C", temp);
        self_test_show_message(GLOBAL_STATE, "TEMP:FAIL");
        tests_done(GLOBAL_STATE, false);
    }
    return temp;
}

static void self_test_start_nonce_measurement(GlobalState * GLOBAL_STATE)
{
    SelfTestNonceMeasurement * measurement = &GLOBAL_STATE->SELF_TEST_MODULE.nonce_measurement;

    pthread_mutex_lock(&measurement->lock);
    measurement->accepted_count = 0;
    measurement->rejected_count = 0;
    measurement->hashes = 0.0;
    measurement->is_active = true;
    pthread_mutex_unlock(&measurement->lock);
}

static void self_test_stop_nonce_measurement(GlobalState * GLOBAL_STATE)
{
    SelfTestNonceMeasurement * measurement = &GLOBAL_STATE->SELF_TEST_MODULE.nonce_measurement;

    pthread_mutex_lock(&measurement->lock);
    measurement->is_active = false;
    pthread_mutex_unlock(&measurement->lock);
}

static void self_test_get_nonce_measurement(GlobalState * GLOBAL_STATE,
                                            uint64_t * accepted_count,
                                            uint64_t * rejected_count,
                                            double * hashes)
{
    SelfTestNonceMeasurement * measurement = &GLOBAL_STATE->SELF_TEST_MODULE.nonce_measurement;

    pthread_mutex_lock(&measurement->lock);
    if (accepted_count) *accepted_count = measurement->accepted_count;
    if (rejected_count) *rejected_count = measurement->rejected_count;
    if (hashes) *hashes = measurement->hashes;
    pthread_mutex_unlock(&measurement->lock);
}

static float self_test_get_nonce_hashrate(GlobalState * GLOBAL_STATE, uint64_t elapsed_us)
{
    if (elapsed_us == 0) {
        return 0.0f;
    }

    double hashes;
    self_test_get_nonce_measurement(GLOBAL_STATE, NULL, NULL, &hashes);

    double seconds = elapsed_us / 1000000.0;
    return (float)(hashes / seconds / 1000000000.0);
}

void self_test_record_nonce(void * pvParameters, double nonce_diff)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SelfTestNonceMeasurement * measurement = &GLOBAL_STATE->SELF_TEST_MODULE.nonce_measurement;
    double ticket_diff = GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty;

    pthread_mutex_lock(&measurement->lock);
    if (measurement->is_active) {
        if (nonce_diff >= ticket_diff) {
            measurement->accepted_count++;
            measurement->hashes += ticket_diff * NONCE_SPACE;
        } else {
            measurement->rejected_count++;
        }
    }
    pthread_mutex_unlock(&measurement->lock);
}

static bool self_test_should_run()
{
    bool is_factory_flash = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF) < 1;
    bool is_self_test_flag_set = nvs_config_get_bool(NVS_CONFIG_SELF_TEST);
    if (is_factory_flash && is_self_test_flag_set) {
        isFactoryTest = true;
        return true;
    }

    // Optionally start self-test when boot button is pressed
    return gpio_get_level(CONFIG_GPIO_BUTTON_BOOT) == 0; // LOW when pressed
}

esp_err_t self_test_init(void * pvParameters)
{
    if (self_test_should_run()) {
        GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

        GLOBAL_STATE->SELF_TEST_MODULE.is_active = true;
        pthread_mutex_init(&GLOBAL_STATE->SELF_TEST_MODULE.nonce_measurement.lock, NULL);
        GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty = DIFFICULTY;
        GLOBAL_STATE->SYSTEM_MODULE.is_connected = true;

        // TODO: This might work here instead of the setup json messages
    // GLOBAL_STATE->extranonce_str = "12905085617eff8e";
    // GLOBAL_STATE->extranonce_2_len = 8;
    // GLOBAL_STATE->pool_difficulty = 0xffffffff;
    // GLOBAL_STATE->new_set_mining_difficulty_msg = true;
    
    // vTaskDelay(1000 / portTICK_PERIOD_MS);

    // No need to set version_mask, it uses default mask which is fine
    // GLOBAL_STATE->version_mask = 0xffffffff;
    // GLOBAL_STATE->new_stratum_version_rolling_msg = true;        

        // Create a binary semaphore
        longPressSemaphore = xSemaphoreCreateBinary();

        if (longPressSemaphore == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

void self_test_reset()
{
    if (longPressSemaphore != NULL) {
        ESP_LOGI(TAG, "Long press detected...");
        // Give the semaphore back
        xSemaphoreGive(longPressSemaphore);
    }
}

void self_test_show_message(void * pvParameters, const char * msg)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    
    if (!GLOBAL_STATE->SELF_TEST_MODULE.is_active) return;

    GLOBAL_STATE->SELF_TEST_MODULE.message = msg;
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

static esp_err_t test_fan_sense(GlobalState * GLOBAL_STATE)
{
    uint16_t fan_speed = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
    uint16_t target_speed = FAN_SPEED_TARGET_MIN;

    ESP_LOGI(TAG, "fanSpeed: %d RPM", fan_speed);
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id == GAMMA_TURBO) {
        target_speed = 500;
    }
    if (fan_speed > target_speed) {
        return ESP_OK;
    }

    // fan test failed
    ESP_LOGE(TAG, "FAN test failed!");
    self_test_show_message(GLOBAL_STATE, "FAN:WARN");
    return ESP_FAIL;
}

static esp_err_t test_power_consumption(GlobalState * GLOBAL_STATE)
{
    float target_power = (float) GLOBAL_STATE->DEVICE_CONFIG.power_consumption_target;
    float margin = (float) POWER_CONSUMPTION_MARGIN;

    float power = 0;
    float current = 0;
    
    Power_get_output(GLOBAL_STATE, &power, &current);
    ESP_LOGI(TAG, "Power: %.2f W", power);

    if (power <= target_power + margin) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "POWER test failed! measured %.2f W, target %.2f W +/- %.2f W", power, target_power, margin);
    self_test_show_message(GLOBAL_STATE, "POWER:FAIL");
    return ESP_FAIL;
}

static esp_err_t test_core_voltage(GlobalState * GLOBAL_STATE)
{
    uint16_t core_voltage = VCORE_get_voltage_mv(GLOBAL_STATE);
    uint16_t target_voltage = GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv;
    float margin = target_voltage * SELF_TEST_CORE_VOLTAGE_TOLERANCE;
    ESP_LOGI(TAG, "Core voltage: %u mV (target: %u mV +/- %.0f mV)", core_voltage, target_voltage, margin);

    if (core_voltage >= target_voltage - margin && core_voltage <= target_voltage + margin) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Core voltage test failed");
    self_test_show_message(GLOBAL_STATE, "VCORE:FAIL");
    return ESP_FAIL;
}

static esp_err_t test_input_voltage(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        return ESP_OK;
    }

    float input_voltage_mv = Power_get_input_voltage(GLOBAL_STATE);
    float nominal_mv = GLOBAL_STATE->DEVICE_CONFIG.family.nominal_voltage * 1000.0f;
    float margin_mv = nominal_mv * INPUT_VOLTAGE_MARGIN;

    ESP_LOGI(TAG, "Input voltage: %.0f mV (nominal: %.0f mV +/- %.0f mV)", input_voltage_mv, nominal_mv, margin_mv);

    if (input_voltage_mv >= nominal_mv - margin_mv && input_voltage_mv <= nominal_mv + margin_mv) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Input voltage test failed! %.0f mV, expected %.0f +/- %.0f mV", input_voltage_mv, nominal_mv, margin_mv);
    self_test_show_message(GLOBAL_STATE, "VIN:FAIL");
    return ESP_FAIL;
}

esp_err_t test_vreg_faults(GlobalState * GLOBAL_STATE)
{
    // check for faults on the voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_check_fault(GLOBAL_STATE), TAG, "VCORE check fault failed!");

    if (GLOBAL_STATE->SYSTEM_MODULE.power_fault) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Perform a self-test of the system.
 *
 * This function is run as a task and will execute a series of
 * diagnostic tests to ensure the system is functioning correctly.
 *
 * @param pvParameters Pointer to the parameters passed to the task (if any).
 */
void self_test_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (!GLOBAL_STATE->SELF_TEST_MODULE.is_active) return;

    // Check if we already have an error message from peripheral initialization
    if (GLOBAL_STATE->SELF_TEST_MODULE.system_init_ret != ESP_OK) {
        ESP_LOGE(TAG, "Aborting self-test due to initialization failure: %s", GLOBAL_STATE->SELF_TEST_MODULE.message);
        tests_done(GLOBAL_STATE, false);
    }

    if (isFactoryTest) {
        ESP_LOGI(TAG, "Running factory self-test");
    } else {
        ESP_LOGI(TAG, "Running manual self-test");
    }

    char logString[300];

    if (!GLOBAL_STATE->psram_is_available) {
        ESP_LOGE(TAG, "NO PSRAM on device!");
        self_test_show_message(GLOBAL_STATE, "PSRAM:FAIL");
        tests_done(GLOBAL_STATE, false);
    }

    // Capture extra validation for DS4432U if present
    if (GLOBAL_STATE->DEVICE_CONFIG.DS4432U && DS4432U_test() != ESP_OK) {
        ESP_LOGE(TAG, "DS4432 test failed!");
        self_test_show_message(GLOBAL_STATE, "DS4432U:FAIL");
        tests_done(GLOBAL_STATE, false);
    }

    // Input voltage check (INA260 devices only)
    if (test_input_voltage(GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Input voltage test failed!");
        self_test_show_message(GLOBAL_STATE, "VOLTAGE:FAIL");
        tests_done(GLOBAL_STATE, false);
    }

    // test for voltage regulator faults
    if (test_vreg_faults(GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "VCORE check fault failed!");
        self_test_show_message(GLOBAL_STATE, "VCORE:PWR FAULT");
        tests_done(GLOBAL_STATE, false);
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // setup and test hashrate
    StratumApiV1Message msg = {0};

    // 1. Mock set_extranonce
    const char *extranonce_json = "{\"id\":null,\"method\":\"mining.set_extranonce\",\"params\":[\"12905085617eff8e\",8]}";
    STRATUM_V1_parse(&msg, extranonce_json);
    if (msg.method == MINING_SET_EXTRANONCE) {
        if (GLOBAL_STATE->extranonce_str) free(GLOBAL_STATE->extranonce_str);
        GLOBAL_STATE->extranonce_str = msg.extranonce_str;
        GLOBAL_STATE->extranonce_2_len = msg.extranonce_2_len;
        ESP_LOGI(TAG, "Self-test: Applied mock extranonce %s, len %d", GLOBAL_STATE->extranonce_str, GLOBAL_STATE->extranonce_2_len);
    }

    // 2. Mock set_difficulty
    memset(&msg, 0, sizeof(msg));
    const char *difficulty_json = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[4294967295]}";
    STRATUM_V1_parse(&msg, difficulty_json);
    if (msg.method == MINING_SET_DIFFICULTY) {
        GLOBAL_STATE->pool_difficulty = msg.new_difficulty;
        GLOBAL_STATE->new_set_mining_difficulty_msg = true;
        ESP_LOGI(TAG, "Self-test: Applied mock difficulty %lu", (unsigned long)GLOBAL_STATE->pool_difficulty);
    }

    // 3. Mock set_version_mask
    memset(&msg, 0, sizeof(msg));
    const char *version_mask_json = "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[\"ffffffff\"]}";
    STRATUM_V1_parse(&msg, version_mask_json);
    if (msg.method == MINING_SET_VERSION_MASK) {
        GLOBAL_STATE->version_mask = msg.version_mask;
        GLOBAL_STATE->new_stratum_version_rolling_msg = true;
        ESP_LOGI(TAG, "Self-test: Applied mock version mask %08lx", GLOBAL_STATE->version_mask);
    }

    // 4. Mock mining.notify
    memset(&msg, 0, sizeof(msg));
    const char *notify_json = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"0\",\"0c859545a3498373a57452fac22eb7113df2a465000543520000000000000000\",\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b0389130cfabe6d6d5cbab26a2599e92916edec5657a94a0708ddb970f5c45b5d\",\"31650707758de07b010000000000001cfd7038212f736c7573682f000000000379ad0c2a000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3ae725d3994b811572c1f345deb98b56b465ef8e153ecbbd27fa37bf1b005161380000000000000000266a24aa21a9ed63b06a7946b190a3fda1d76165b25c9b883bcc6621b040773050ee2a1bb18f1800000000\",[\"2b77d9e413e8121cd7a17ff46029591051d0922bd90b2b2a38811af1cb57a2b2\",\"5c8874cef00f3a233939516950e160949ef327891c9090467cead995441d22c5\",\"2d91ff8e19ac5fa69a40081f26c5852d366d608b04d2efe0d5b65d111d0d8074\",\"0ae96f609ad2264112a0b2dfb65624bedbcea3b036a59c0173394bba3a74e887\",\"e62172e63973d69574a82828aeb5711fc5ff97946db10fc7ec32830b24df7bde\",\"adb49456453aab49549a9eb46bb26787fb538e0a5f656992275194c04651ec97\",\"a7bc56d04d2672a8683892d6c8d376c73d250a4871fdf6f57019bcc737d6d2c2\",\"d94eceb8182b4f418cd071e93ec2a8993a0898d4c93bc33d9302f60dbbd0ed10\",\"5ad7788b8c66f8f50d332b88a80077ce10e54281ca472b4ed9bbbbcb6cf99083\",\"9f9d784b33df1b3ed3edb4211afc0dc1909af9758c6f8267e469f5148ed04809\",\"48fd17affa76b23e6fb2257df30374da839d6cb264656a82e34b350722b05123\",\"c4f5ab01913fc186d550c1a28f3f3e9ffaca2016b961a6a751f8cca0089df924\",\"cff737e1d00176dd6bbfa73071adbb370f227cfb5fba186562e4060fcec877e1\"],\"20000004\",\"1705ae3a\",\"647025b5\",true]}";
    STRATUM_V1_parse(&msg, notify_json);

    if (msg.method == MINING_NOTIFY) {
        ESP_LOGI(TAG, "Enqueuing mock work into stratum_queue");
        queue_enqueue(&GLOBAL_STATE->stratum_queue, msg.mining_notification);
    } else {
        ESP_LOGE(TAG, "Failed to parse mock mining notification");
        tests_done(GLOBAL_STATE, false);
    }

    self_test_set_fan_percent(GLOBAL_STATE, SELF_TEST_MAX_FAN_PERCENT);

    float target_temp = (float)nvs_config_get_u16(NVS_CONFIG_SELF_TEST_TEMP_TARGET);
    float warmup_temp = (float)nvs_config_get_u16(NVS_CONFIG_SELF_TEST_TEMP_WARMUP);
    float max_temp    = (float)nvs_config_get_u16(NVS_CONFIG_SELF_TEST_TEMP_MAX);

    float asic_temp = self_test_get_valid_control_temp(GLOBAL_STATE);
    ESP_LOGI(TAG, "ASIC Temp %.1f°C", asic_temp);

    self_test_set_fan_percent(GLOBAL_STATE, SELF_TEST_MIN_FAN_PERCENT);
    while (asic_temp < warmup_temp)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        asic_temp = self_test_get_valid_control_temp(GLOBAL_STATE);
        ESP_LOGI(TAG, "Warming up to %.1f°C: %.1f°C", warmup_temp, asic_temp);
        snprintf(logString, sizeof(logString), "ASIC Temp: %.1f°C", asic_temp);
        self_test_show_message(GLOBAL_STATE, logString);
    }

    PIDController pid = {0};
    float pid_input = asic_temp;
    float pid_output = SELF_TEST_MIN_FAN_PERCENT;
    float pid_setpoint = target_temp;
    pid_init(&pid, &pid_input, &pid_output, &pid_setpoint,
             SELF_TEST_PID_P, SELF_TEST_PID_I, SELF_TEST_PID_D, PID_P_ON_E, PID_REVERSE);
    pid_set_sample_time(&pid, SELF_TEST_PID_SAMPLE_TIME_MS);
    pid_set_output_limits(&pid, SELF_TEST_MIN_FAN_PERCENT, SELF_TEST_MAX_FAN_PERCENT);
    pid_set_mode(&pid, AUTOMATIC);

    uint64_t start_us = esp_timer_get_time();
    uint64_t hashtest_us = 30000000;
    float hashrate = 0;
    float expected_hashrate = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value *
                              GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
                              GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f *
                              GLOBAL_STATE->DEVICE_CONFIG.family.asic.hashrate_test_percentage_target;
    float expected_domain_hashrate = expected_hashrate /
                                     GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains /
                                     GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;

    SelfTestDomainAverages domain_averages;
    if (self_test_domain_averages_init(&domain_averages,
                                       GLOBAL_STATE->DEVICE_CONFIG.family.asic_count,
                                       GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate domain hashrate averages");
        self_test_show_message(GLOBAL_STATE, "MEM:FAIL");
        tests_done(GLOBAL_STATE, false);
    }
    self_test_domain_averages_prime(GLOBAL_STATE, &domain_averages);

    self_test_start_nonce_measurement(GLOBAL_STATE);
    ESP_LOGI(TAG, "Starting 30s hashrate monitoring loop, target temp %.1f°C", target_temp);
    while ((esp_timer_get_time() - start_us) < hashtest_us) {
        uint64_t elapsed_us = esp_timer_get_time() - start_us;
        hashrate = self_test_get_nonce_hashrate(GLOBAL_STATE, elapsed_us);
        asic_temp = self_test_get_valid_control_temp(GLOBAL_STATE);
        pid_input = asic_temp;
        if (pid_compute(&pid)) {
            self_test_set_fan_percent(GLOBAL_STATE, pid_output);
        }
        
        if (asic_temp > max_temp) {
            ESP_LOGE(TAG, "Overheat: %.1f°C", asic_temp);
            snprintf(logString, sizeof(logString), "TEMP:FAIL: %.1f°C", asic_temp);
            self_test_show_message(GLOBAL_STATE, logString);
            tests_done(GLOBAL_STATE, false);
        }

        uint32_t remaining = elapsed_us < hashtest_us ? (hashtest_us - elapsed_us) / 1000000 : 0;
        snprintf(logString, sizeof(logString), "%.0f Gh/s %.1f°C %" PRIu32 "s", hashrate, asic_temp, remaining);
        ESP_LOGI(TAG, "%s", logString);

        self_test_show_message(GLOBAL_STATE, logString);

        self_test_domain_averages_sample(GLOBAL_STATE, &domain_averages, expected_domain_hashrate);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    self_test_stop_nonce_measurement(GLOBAL_STATE);
    hashrate = self_test_get_nonce_hashrate(GLOBAL_STATE, esp_timer_get_time() - start_us);
    self_test_domain_averages_sample(GLOBAL_STATE, &domain_averages, expected_domain_hashrate);

    uint64_t accepted_count;
    uint64_t rejected_count;
    self_test_get_nonce_measurement(GLOBAL_STATE, &accepted_count, &rejected_count, NULL);
    ESP_LOGI(TAG, "Hashrate: %.2f Gh/s, Expected: %.2f Gh/s", hashrate, expected_hashrate);
    ESP_LOGI(TAG,
             "Nonce measurement: %llu valid, %llu rejected",
             (unsigned long long)accepted_count,
             (unsigned long long)rejected_count);

    // Check domain hashrates from monitor module
    bool domain_failed = false;
    uint32_t failed_asic_mask = 0;
    for (int asic_nr = 0; asic_nr < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count; asic_nr++) {
        int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;
        for (int domain_nr = 0; domain_nr < hash_domains; domain_nr++) {
            const SelfTestDomainAverage * domain_average = self_test_domain_get_const(&domain_averages, asic_nr, domain_nr);
            float domain_hashrate = self_test_domain_average_hashrate(domain_average);
            uint32_t sample_count = domain_average->sample_count;
            uint32_t rejected_sample_count = domain_average->rejected_sample_count;
            uint32_t total_domain_samples = sample_count + rejected_sample_count;
            SelfTestDomainStatus domain_status = SELF_TEST_DOMAIN_OK;

            ESP_LOGI(TAG,
                     "ASIC %d Domain %d Average Hashrate: %.2f Gh/s (%lu samples, %lu rejected)",
                     asic_nr,
                     domain_nr,
                     domain_hashrate,
                     (unsigned long)sample_count,
                     (unsigned long)rejected_sample_count);
            if (rejected_sample_count > 0) {
                ESP_LOGW(TAG,
                         "ASIC %d Domain %d ignored %lu implausible register sample(s); using nonce hashrate for total validation",
                         asic_nr,
                         domain_nr,
                         (unsigned long)rejected_sample_count);
            }
            
            float min_domain_hashrate = expected_domain_hashrate * (1.0f - SELF_TEST_DOMAIN_HASHRATE_TOLERANCE);
            float max_domain_hashrate = expected_domain_hashrate * (1.0f + SELF_TEST_DOMAIN_HASHRATE_TOLERANCE);
            if (sample_count == 0 && rejected_sample_count > 0) {
                domain_status = SELF_TEST_DOMAIN_UNRELIABLE;
                ESP_LOGW(TAG,
                         "ASIC %d Domain %d self-reported counter is unreliable; all %lu sample(s) were implausible high, external nonce hashrate remains authoritative",
                         asic_nr,
                         domain_nr,
                         (unsigned long)rejected_sample_count);
            } else if (total_domain_samples > 0 &&
                       ((float)rejected_sample_count / (float)total_domain_samples) >= SELF_TEST_DOMAIN_REJECTED_WARN_RATIO) {
                domain_status = SELF_TEST_DOMAIN_UNRELIABLE;
                ESP_LOGW(TAG,
                         "ASIC %d Domain %d self-reported counter is unstable; %lu/%lu sample(s) were implausible, external nonce hashrate remains authoritative",
                         asic_nr,
                         domain_nr,
                         (unsigned long)rejected_sample_count,
                         (unsigned long)total_domain_samples);
            } else if (sample_count == 0 || domain_hashrate < min_domain_hashrate || domain_hashrate > max_domain_hashrate) {
                domain_status = SELF_TEST_DOMAIN_FAIL;
                ESP_LOGE(TAG,
                         "ASIC %d Domain %d:FAIL - hashrate %.2f Gh/s, expected %.2f-%.2f Gh/s",
                         asic_nr,
                         domain_nr,
                         domain_hashrate,
                         min_domain_hashrate,
                         max_domain_hashrate);
            }

            if (domain_status == SELF_TEST_DOMAIN_FAIL) {
                domain_failed = true;
                if (asic_nr < 32) {
                    failed_asic_mask |= (1u << asic_nr);
                }
            }
        }
    }
    self_test_domain_averages_free(&domain_averages);
    if (domain_failed) {
        if (GLOBAL_STATE->DEVICE_CONFIG.family.asic_count == 2 && failed_asic_mask == 0x3) {
            self_test_show_message(GLOBAL_STATE, "BOTH ASICS DOMAIN:FAIL");
        } else {
            int failed_asic = -1;
            for (int asic_nr = 0; asic_nr < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count && asic_nr < 32; asic_nr++) {
                if (failed_asic_mask & (1u << asic_nr)) {
                    failed_asic = asic_nr;
                    break;
                }
            }

            if (failed_asic >= 0) {
                snprintf(logString, sizeof(logString), "ASIC %d DOMAIN:FAIL", failed_asic);
                self_test_show_message(GLOBAL_STATE, logString);
            } else {
                self_test_show_message(GLOBAL_STATE, "DOMAIN:FAIL");
            }
        }
        tests_done(GLOBAL_STATE, false);
    }

    if (hashrate < expected_hashrate) {
        ESP_LOGE(TAG, "Total hashrate too low");
        self_test_show_message(GLOBAL_STATE, "HASHRATE:FAIL");
        tests_done(GLOBAL_STATE, false);
    }

    if (test_core_voltage(GLOBAL_STATE) != ESP_OK) {
        tests_done(GLOBAL_STATE, false);
    }

    if (test_power_consumption(GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Power Draw Failed, target %.2f W", (float) GLOBAL_STATE->DEVICE_CONFIG.power_consumption_target);
        self_test_show_message(GLOBAL_STATE, "POWER:FAIL");
        tests_done(GLOBAL_STATE, false);
    }

    if (test_fan_sense(GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Fan test failed!");
        tests_done(GLOBAL_STATE, false);
    }

    tests_done(GLOBAL_STATE, true);
}

/**
 * Ends the self test by either resetting or ending the self_test_task
 */
static void tests_done(GlobalState * GLOBAL_STATE, bool isTestPassed)
{
    GLOBAL_STATE->SELF_TEST_MODULE.is_finished = true;
    self_test_stop_nonce_measurement(GLOBAL_STATE);
    VCORE_set_voltage(GLOBAL_STATE, 0.0f);
    asic_hold_reset_low();

    if (isTestPassed) {
        if (isFactoryTest) {
            ESP_LOGI(TAG, "Self-test flag cleared");
            nvs_config_set_bool(NVS_CONFIG_SELF_TEST, false);
        }
        ESP_LOGI(TAG, "SELF-TEST PASS! -- Restarting in 10 seconds.");
        GLOBAL_STATE->SELF_TEST_MODULE.result = "SELF-TEST PASS!";
        char logString[21];
        for (int i = 10; i > 0; i--) {
            snprintf(logString, sizeof(logString), "Restarting in %d...", i);
            GLOBAL_STATE->SELF_TEST_MODULE.finished = logString;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        esp_restart();
    } else {
        // isTestFailed
        GLOBAL_STATE->SELF_TEST_MODULE.result = "SELF-TEST FAIL!";
        if (isFactoryTest) {
            ESP_LOGI(
                TAG,
                "SELF-TEST FAIL! -- Hold BOOT button for 2 seconds to cancel self-test, or press RESET to run self-test again.");
            GLOBAL_STATE->SELF_TEST_MODULE.finished =
                "Hold BOOT button for 2 seconds to cancel self-test, or press RESET to run self-test again.";
        } else {
            ESP_LOGI(TAG, "SELF-TEST FAIL -- Press RESET button to restart.");
            GLOBAL_STATE->SELF_TEST_MODULE.finished = "Press RESET button to restart.";
        }
        while (1) {
            // Wait here forever until reset_self_test() gives the longPressSemaphore
            if (xSemaphoreTake(longPressSemaphore, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "Self-test flag cleared");
                nvs_config_set_bool(NVS_CONFIG_SELF_TEST, false);
                // Wait until NVS is written
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_restart();
            }
        }
    }

    vTaskDelete(NULL);
}
