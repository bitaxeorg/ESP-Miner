#include <string.h>
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

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE

/////Test Constants/////
// Test Fan Speed
#define FAN_SPEED_TARGET_MIN 1000 // RPM

// Test Core Voltage
#define CORE_VOLTAGE_TARGET_MIN 1000 // mV
#define CORE_VOLTAGE_TARGET_MAX 1300 // mV

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

void self_test_show_message(void * pvParameters, char * msg)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    
    if (!GLOBAL_STATE->SELF_TEST_MODULE.is_active) return;

    GLOBAL_STATE->SELF_TEST_MODULE.message = msg;
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

static esp_err_t test_fan_sense(GlobalState * GLOBAL_STATE)
{
    uint16_t fan_speed = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
    ESP_LOGI(TAG, "fanSpeed: %d RPM", fan_speed);
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.id) {
        case GAMMA:
            if (fan_speed > 1000) {
                return ESP_OK;
            }
            break;
        case GAMMA_TURBO:
            if (fan_speed > 500) {
                return ESP_OK;
            }
            break;
        default:
            if (fan_speed > 1000) {
                return ESP_OK;
            }
            break;
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
    ESP_LOGI(TAG, "Voltage: %u mV", core_voltage);

    if (core_voltage > CORE_VOLTAGE_TARGET_MIN && core_voltage < CORE_VOLTAGE_TARGET_MAX) {
        return ESP_OK;
    }
    // tests failed
    ESP_LOGE(TAG, "Core Voltage TEST FAIL, INCORRECT CORE VOLTAGE");
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

    Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1);

    float asic_temp = Thermal_get_chip_temp(GLOBAL_STATE);
    ESP_LOGI(TAG, "ASIC Temp %.1f°C", asic_temp);

    // detect open circuit / no result
    if (asic_temp == -1.0 || asic_temp == 127.0) {
        ESP_LOGE(TAG, "Open circuit or no result on temperature sensor: %.1f°C", asic_temp);
        snprintf(logString, sizeof(logString), "TEMP:FAIL: %.1f°C", asic_temp);
        self_test_show_message(GLOBAL_STATE, logString);
        tests_done(GLOBAL_STATE, false);
    }

    Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 0.1f);
    while (asic_temp < 50.0f)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        asic_temp = Thermal_get_chip_temp(GLOBAL_STATE);
        ESP_LOGI(TAG, "Warming up: %.1f°C", asic_temp);
        snprintf(logString, sizeof(logString), "ASIC Temp: %.1f°C", asic_temp);
        self_test_show_message(GLOBAL_STATE, logString);
    }
    Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1.0f);

    uint32_t start_ms = esp_timer_get_time() / 1000;
    uint32_t hashtest_ms = 30000;
    float hashrate = 0;

    ESP_LOGI(TAG, "Starting 30s hashrate monitoring loop");
    while ((esp_timer_get_time() / 1000) - start_ms < hashtest_ms) {
        hashrate = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
        asic_temp = Thermal_get_chip_temp(GLOBAL_STATE);
        
        if (asic_temp > 62) {
            ESP_LOGE(TAG, "Overheat: %.1f°C", asic_temp);
            snprintf(logString, sizeof(logString), "TEMP:FAIL: %.1f°C", asic_temp);
            self_test_show_message(GLOBAL_STATE, logString);
            tests_done(GLOBAL_STATE, false);
        }

        uint32_t remaining = (hashtest_ms - ((esp_timer_get_time() / 1000) - start_ms)) / 1000;
        snprintf(logString, sizeof(logString), "%.0f Gh/s %.1f°C %lds", hashrate, asic_temp, remaining);
        ESP_LOGI(TAG, "%s", logString);

        self_test_show_message(GLOBAL_STATE, logString);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    float expected_hashrate_mhs = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value *
                                  GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
                                  GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f *
                                  GLOBAL_STATE->DEVICE_CONFIG.family.asic.hashrate_test_percentage_target;

    ESP_LOGI(TAG, "Hashrate: %.2f Gh/s, Expected: %.2f Gh/s", hashrate, expected_hashrate_mhs);

    if (hashrate < expected_hashrate_mhs) {
        ESP_LOGE(TAG, "Total hashrate too low");
        self_test_show_message(GLOBAL_STATE, "HASHRATE:FAIL");
        tests_done(GLOBAL_STATE, false);
    }

    // Check domain hashrates from monitor module
    HashrateMonitorModule * monitor = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    for (int asic_nr = 0; asic_nr < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count; asic_nr++) {
        int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;
        for (int domain_nr = 0; domain_nr < hash_domains; domain_nr++) {
            float domain_hashrate = monitor->domain_measurements[asic_nr][domain_nr].hashrate;
            ESP_LOGI(TAG, "ASIC %d Domain %d Hashrate: %.2f Gh/s", asic_nr, domain_nr, domain_hashrate);
            
            float expected_domain_hashrate = expected_hashrate_mhs / hash_domains / GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
            if(domain_hashrate < expected_domain_hashrate / 3 || domain_hashrate > expected_domain_hashrate * 3) {
                ESP_LOGE(TAG, "ASIC %d Domain %d:FAIL - hashrate %.2f Gh/s, expected ~%.2f Gh/s", asic_nr, domain_nr, domain_hashrate, expected_domain_hashrate);
                char error_buf[30];
                snprintf(error_buf, 30, "ASIC %d DOMAIN %d:FAIL", asic_nr, domain_nr);
                self_test_show_message(GLOBAL_STATE, error_buf);
                tests_done(GLOBAL_STATE, false);
            }
        }
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
