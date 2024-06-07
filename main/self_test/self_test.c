#include "i2c_master.h"
#include "DS4432U.h"
#include "EMC2101.h"
#include "INA260.h"
#include "adc.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include "nvs_flash.h"
#include "oled.h"
#include "vcore.h"
#include "utils.h"

static const char * TAG = "self_test";

static bool fan_sense_pass()
{
    uint16_t fan_speed = EMC2101_get_fan_speed();
    ESP_LOGI(TAG, "fanSpeed: %d", fan_speed);
    if (fan_speed > 1000) {
        return true;
    }
    return false;
}

static bool power_consumption_pass()
{
    float power = INA260_read_power() / 1000;
    ESP_LOGI(TAG, "Power: %f", power);
    if (power > 9 && power < 15) {
        return true;
    }
    return false;
}

static bool core_voltage_pass(GlobalState * global_state)
{
    uint16_t core_voltage = VCORE_get_voltage_mv(global_state);
    ESP_LOGI(TAG, "Voltage: %u", core_voltage);

    if (core_voltage > 1100 && core_voltage < 1300) {
        return true;
    }
    return false;
}

void self_test(void * pvParameters)
{

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);

    for (int i = 0; i < 128; i++) {

        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }

    // turn ASIC on
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 0);

    // Init I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    VCORE_init(GLOBAL_STATE);
    VCORE_set_voltage(nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) / 1000.0, GLOBAL_STATE);

    EMC2101_init(nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, 1));
    EMC2101_set_fan_speed(1);

    // oled
    if (!OLED_init()) {
        ESP_LOGI(TAG, "OLED init failed!");
    } else {
        ESP_LOGI(TAG, "OLED init success!");
        // clear the oled screen
        OLED_fill(0);
    }

    if (OLED_status()) {
        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "SELF TEST...");
        OLED_writeString(0, 0, module->oled_buf);
    }

    if(!DS4432U_test()){
        if (OLED_status()) {
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, "DS4432U:FAIL");
            OLED_writeString(0, 2, module->oled_buf);
        }
    }


    SERIAL_init();
    uint8_t chips_detected = (GLOBAL_STATE->ASIC_functions.init_fn)(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value);
    ESP_LOGI(TAG, "%u chips detected", chips_detected);

    int baud = (*GLOBAL_STATE->ASIC_functions.set_max_baud_fn)();
    vTaskDelay(10 / portTICK_PERIOD_MS);
    SERIAL_set_baud(baud);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    mining_notify notify_message;
    notify_message.job_id = 0;
    notify_message.prev_block_hash = "0c859545a3498373a57452fac22eb7113df2a465000543520000000000000000";
    notify_message.version = 0x20000004;
    notify_message.version_mask = 0x1fffe000;
    notify_message.target = 0x1705ae3a;
    notify_message.ntime = 0x647025b5;
    notify_message.difficulty = 1000000;

    const char * coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b0389130cfab"
                               "e6d6d5cbab26a2599e92916edec"
                               "5657a94a0708ddb970f5c45b5d12905085617eff8e010000000000000031650707758de07b010000000000001cfd703"
                               "8212f736c7573682f0000000003"
                               "79ad0c2a000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c29525"
                               "34b424c4f434b3ae725d3994b81"
                               "1572c1f345deb98b56b465ef8e153ecbbd27fa37bf1b005161380000000000000000266a24aa21a9ed63b06a7946b19"
                               "0a3fda1d76165b25c9b883bcc66"
                               "21b040773050ee2a1bb18f1800000000";
    uint8_t merkles[13][32];
    int num_merkles = 13;

    hex2bin("2b77d9e413e8121cd7a17ff46029591051d0922bd90b2b2a38811af1cb57a2b2", merkles[0], 32);
    hex2bin("5c8874cef00f3a233939516950e160949ef327891c9090467cead995441d22c5", merkles[1], 32);
    hex2bin("2d91ff8e19ac5fa69a40081f26c5852d366d608b04d2efe0d5b65d111d0d8074", merkles[2], 32);
    hex2bin("0ae96f609ad2264112a0b2dfb65624bedbcea3b036a59c0173394bba3a74e887", merkles[3], 32);
    hex2bin("e62172e63973d69574a82828aeb5711fc5ff97946db10fc7ec32830b24df7bde", merkles[4], 32);
    hex2bin("adb49456453aab49549a9eb46bb26787fb538e0a5f656992275194c04651ec97", merkles[5], 32);
    hex2bin("a7bc56d04d2672a8683892d6c8d376c73d250a4871fdf6f57019bcc737d6d2c2", merkles[6], 32);
    hex2bin("d94eceb8182b4f418cd071e93ec2a8993a0898d4c93bc33d9302f60dbbd0ed10", merkles[7], 32);
    hex2bin("5ad7788b8c66f8f50d332b88a80077ce10e54281ca472b4ed9bbbbcb6cf99083", merkles[8], 32);
    hex2bin("9f9d784b33df1b3ed3edb4211afc0dc1909af9758c6f8267e469f5148ed04809", merkles[9], 32);
    hex2bin("48fd17affa76b23e6fb2257df30374da839d6cb264656a82e34b350722b05123", merkles[10], 32);
    hex2bin("c4f5ab01913fc186d550c1a28f3f3e9ffaca2016b961a6a751f8cca0089df924", merkles[11], 32);
    hex2bin("cff737e1d00176dd6bbfa73071adbb370f227cfb5fba186562e4060fcec877e1", merkles[12], 32);

    char * merkle_root = calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles);

    bm_job job = construct_bm_job(&notify_message, merkle_root, 0x1fffe000);

    (*GLOBAL_STATE->ASIC_functions.set_difficulty_mask_fn)(32);

    ESP_LOGI(TAG, "Sending work");

    (*GLOBAL_STATE->ASIC_functions.send_work_fn)(GLOBAL_STATE, &job);
    // vTaskDelay((GLOBAL_STATE->asic_job_frequency_ms - 0.3) / portTICK_PERIOD_MS);

    // ESP_LOGI(TAG, "Receiving work");

    // task_result * asic_result = (*GLOBAL_STATE->ASIC_functions.receive_result_fn)(GLOBAL_STATE);
    // if (asic_result != NULL) {

    //     ESP_LOGI(TAG, "Received work");

    //     // check the nonce difficulty
    //     double nonce_diff = test_nonce_value(&job, asic_result->nonce, asic_result->rolled_version);
    //     ESP_LOGI(TAG, "Nonce %lu Nonce difficulty %.32f.", asic_result->nonce, nonce_diff);

    // if (asic_result->nonce == 4054974794) {
    if (chips_detected < 1) {
        ESP_LOGE(TAG, "SELF TEST FAIL, NO CHIPS DETECTED");
        // ESP_LOGE(TAG, "SELF TEST FAIL, INCORRECT NONCE DIFF");
        if (OLED_status()) {
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, "ASIC:FAIL NO CHIPS");
            OLED_writeString(0, 2, module->oled_buf);
        }
        return;
    }

    free(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs);
    free(GLOBAL_STATE->valid_jobs);

    if (!core_voltage_pass(GLOBAL_STATE)) {
        if (OLED_status()) {
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, "POWER:     FAIL");
            OLED_writeString(0, 2, module->oled_buf);
        }
        return;
    }

    if (INA260_installed() && !power_consumption_pass()) {
        if (OLED_status()) {
            memset(module->oled_buf, 0, 20);
            snprintf(module->oled_buf, 20, "POWER:     FAIL");
            OLED_writeString(0, 2, module->oled_buf);
        }
        return;
    }

    if (!fan_sense_pass()) {
        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "FAN:       WARN");
        OLED_writeString(0, 1, module->oled_buf);
    }


    memset(module->oled_buf, 0, 20);
    snprintf(module->oled_buf, 20, "           PASS");
    OLED_writeString(0, 2, module->oled_buf);
    nvs_config_set_u16(NVS_CONFIG_SELF_TEST, 0);

}
