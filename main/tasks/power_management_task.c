#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "thermal.h"
#include "power.h"
#include "asic.h"
#include "asic_common.h"
#include "utils.h"
#include "asic_init.h"
#include "asic_reset.h"
#include "driver/uart.h"

#define POLL_RATE 100
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define SAFE_TEMP 45.0

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

#define ASIC_REDUCTION 100.0

// Tempo estavel (sem overheat, temperatura segura) necessario antes de tentar
// devolver a placa a configuracao que o usuario definiu. Uma reducao por
// overheat NAO deve ser permanente: sem isso, um unico disparo rebaixa a placa
// de 650 para 550 MHz para sempre e o usuario so descobre pela queda de hashrate.
#define RESTORE_STABLE_US (30LL * 60 * 1000000)

// Quantas vezes, por sessao, vale tentar restaurar. Se a placa voltar a
// esquentar depois de restaurada, insistir so criaria oscilacao termica.
#define RESTORE_MAX_ATTEMPTS 2

static const char * TAG = "power_management";

static void mining_stop(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Stopping mining");

    // Wind frequency down to 50 MHz before cutting power. This also updates
    // the transition tracker so the ramp starts from 50 MHz on next start,
    // rather than the stale pre-reset frequency.
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = 50;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = 0;

    ASIC_set_frequency(GLOBAL_STATE);
    ASIC_set_nonce_space(GLOBAL_STATE);

    // Cut ASIC power and hold in reset
    VCORE_set_voltage(GLOBAL_STATE, 0.0f);
    asic_hold_reset_low();

    // Mark uninitialized immediately so tasks stop issuing UART commands
    GLOBAL_STATE->ASIC_initalized = false;

    // Give tasks time to complete any in-progress UART operation
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Flush any stale data from the UART buffers
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Mining stopped");
}

static uint8_t mining_start(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Starting mining");

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);

    // Restore voltage from NVS
    uint16_t voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
    VCORE_set_voltage(GLOBAL_STATE, (double) voltage / 1000.0);

    // Wait for voltage to stabilize before touching the ASIC
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Clear any accumulated UART garbage before init
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Stabilization delay of 2000ms prevents race conditions where tasks are
    // just starting to use the ASIC while power management tries to change frequency
    uint8_t chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, 2000);

    if (chip_count > 0) {
        ESP_LOGI(TAG, "Mining started successfully (%d chip(s))", chip_count);
    } else {
        ESP_LOGE(TAG, "Mining start failed - ASIC not detected");
    }

    return chip_count;
}

static float expected_hashrate(GlobalState * GLOBAL_STATE)
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0;
}

float POWER_MANAGEMENT_get_nominal_hashrate(const GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate > 0.0f) {
        return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate;
    }

    float freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    if (freq <= 0.0f) {
        freq = (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_frequency_mhz;
    }

    return (freq * (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic_count) / 1000.0f;
}

void POWER_MANAGEMENT_init_frequency(GlobalState * GLOBAL_STATE)
{
    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = frequency;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = 50.0;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = expected_hashrate(GLOBAL_STATE);
    
    char expected_hashrate_str[16] = {0};
    suffixString(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate * 1e6, expected_hashrate_str, sizeof(expected_hashrate_str), 0);
    ESP_LOGI(TAG, "ASIC Frequency: %g MHz, Expected hashrate: %sH/s", frequency, expected_hashrate_str);
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);
    
    float last_asic_frequency = power_management->frequency_value;

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;

    uint16_t last_known_asic_voltage = 0;
    float last_known_asic_frequency = 0.0;
    bool is_paused = false;
    int overheat_streak = 0;

    // Recuperacao pos-overheat: instante em que a placa comecou a rodar estavel
    // na configuracao reduzida, e quantas restauracoes ja foram tentadas.
    int64_t stable_since_us = 0;
    int restore_attempts = 0;

    while (1) {
        if (GLOBAL_STATE->SELF_TEST_MODULE.is_finished) {
            ESP_LOGI(TAG, "Stopped");
            vTaskDelete(NULL);
            return;
        }

        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        Power_get_output(GLOBAL_STATE, &power_management->power, &power_management->current);
        power_management->core_voltage = VCORE_get_voltage_mv(GLOBAL_STATE);

        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
        power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        // User pause, hardware fault, or all pools unreachable
        bool wants_stop = sys_module->mining_paused || sys_module->hardware_fault || sys_module->pools_unavailable;
        if (wants_stop && !is_paused) {
            mining_stop(GLOBAL_STATE);
            is_paused = true;
        } else if (!wants_stop && is_paused) {
            mining_start(GLOBAL_STATE);
            is_paused = false;
        }

        // If we've paused or have a hardware fault, skip doing anything else
        if (is_paused || sys_module->hardware_fault) {
            vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
            continue;
        }

        bool asic_overheat =
            power_management->chip_temp_avg > THROTTLE_TEMP
            || power_management->chip_temp2_avg > THROTTLE_TEMP;

        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || asic_overheat) && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            if (power_management->chip_temp2_avg > 0) {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC1: %fC ASIC2: %fC", power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
            } else {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC: %fC", power_management->vr_temp, power_management->chip_temp_avg);
            }

            last_known_asic_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
            last_known_asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

            // Guarda a configuracao do usuario antes de rebaixar. Se a chave ja
            // existe, significa que uma reducao anterior ainda nao foi
            // restaurada — nao sobrescrever, senao a config boa se perde de vez.
            if (!nvs_config_has_key(NVS_CONFIG_USER_FREQUENCY)) {
                nvs_config_set_float(NVS_CONFIG_USER_FREQUENCY, last_known_asic_frequency);
                nvs_config_set_u16(NVS_CONFIG_USER_VOLTAGE, last_known_asic_voltage);
                ESP_LOGW(TAG, "Config do usuario preservada para restauracao: %.0f MHz / %umV",
                         last_known_asic_frequency, last_known_asic_voltage);
            }
            nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, false);
            nvs_config_set_u16(NVS_CONFIG_MANUAL_FAN_SPEED, 100);
            nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, true);
            ESP_LOGW(TAG, "Entering safe mode due to overheat condition. System operation halted.");
            mining_stop(GLOBAL_STATE);
            
            // Note: ASIC temperature readings are invalid when ASIC is powered down (returns -1)
            // For 600-series boards that use ASIC thermal diode, we rely on VR temp and fixed cooling time
            // For boards with EMC internal temp sensor, readings remain valid
            bool asic_temp_valid = GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp;
            int cooling_cycles = 0;
            const int MIN_COOLING_CYCLES = 6; // Minimum 30 seconds cooling
            
            while (cooling_cycles < MIN_COOLING_CYCLES || power_management->vr_temp > TPS546_THROTTLE_TEMP - 10) {
                vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait 5 seconds
                cooling_cycles++;
                
                power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
                
                // Only check ASIC temps if they're valid (not using ASIC thermal diode)
                if (asic_temp_valid) {
                    power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
                    power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);
                    ESP_LOGW(TAG, "Safe mode active (cycle %d) - VR: %.1f°C ASIC1: %.1f°C ASIC2: %.1f°C",
                             cooling_cycles, power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
                    
                    // Continue if ASIC temps still too high
                    if (power_management->chip_temp_avg >  SAFE_TEMP || power_management->chip_temp2_avg > SAFE_TEMP) {
                        cooling_cycles = 0; // Reset cycle count if still hot
                    }
                } else {
                    // For boards using ASIC thermal diode (600 series), rely on VR temp and time
                    ESP_LOGW(TAG, "Safe mode active (cycle %d/%d) - VR: %.1f°C (ASIC temps unavailable while powered down)",
                             cooling_cycles, MIN_COOLING_CYCLES, power_management->vr_temp);
                }
            }
            ESP_LOGI(TAG, "Temperature normalized after %d cooling cycles. Reinitializing ASIC...", cooling_cycles);
            
            uint16_t reduced_voltage = last_known_asic_voltage > ASIC_REDUCTION ? last_known_asic_voltage - ASIC_REDUCTION : 1000;
            float reduced_asic_frequency = last_known_asic_frequency > ASIC_REDUCTION ? last_known_asic_frequency - ASIC_REDUCTION : 400.0;

            // Never drop below the regulator's minimum core voltage. TPS546_set_vout()
            // rejects anything lower (out of range), which leaves the VR stuck in a
            // "power fault" — and the invalid value is persisted to NVS, so it survives
            // reboots. Frequency reduction still provides the cooling headroom.
            int16_t min_voltage = VCORE_get_voltage_min_mv(GLOBAL_STATE);
            if (min_voltage > 0 && reduced_voltage < min_voltage) {
                reduced_voltage = (uint16_t) min_voltage;
            }

            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, reduced_voltage);
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, reduced_asic_frequency);
            
            ESP_LOGI(TAG, "Restoring at reduced settings: %umV (was %umV), %.0f MHz (was %.0f MHz)",
                     reduced_voltage, last_known_asic_voltage, reduced_asic_frequency, last_known_asic_frequency);

            uint8_t chip_count = mining_start(GLOBAL_STATE);

            if (chip_count > 0) {
                // Frequency reduction will now be applied by normal power management loop
                nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, false);
                // A contagem de estabilidade comeca agora: se a placa ficar
                // segura por RESTORE_STABLE_US, ela recupera a config do usuario.
                stable_since_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Resuming normal operation. Reduced frequency (%.0f MHz) will be applied automatically.", reduced_asic_frequency);
            }
        }

        // Post-overheat recovery: if the board is below the user's configured
        // operating point and has stayed cool long enough, restore the original
        // settings. A reduction caused by one overheat event must not be permanent.
        if (stable_since_us > 0 && restore_attempts < RESTORE_MAX_ATTEMPTS
            && nvs_config_has_key(NVS_CONFIG_USER_FREQUENCY)) {
            float user_freq = nvs_config_get_float(NVS_CONFIG_USER_FREQUENCY);
            uint16_t user_volt = nvs_config_get_u16(NVS_CONFIG_USER_VOLTAGE);
            float cur_freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
            uint16_t cur_volt = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);

            if (user_freq <= cur_freq + 1.0f) {
                // Already at the user's setting: nothing to restore, but drop the
                // saved keys so a future overheat can record a fresh baseline.
                nvs_config_clear_key(NVS_CONFIG_USER_FREQUENCY);
                nvs_config_clear_key(NVS_CONFIG_USER_VOLTAGE);
                stable_since_us = 0;
            } else {
                // Re-read temperatures NOW: the overheat block above may have
                // refreshed the readings during cooling.
                bool vr_ok = power_management->vr_temp < TPS546_THROTTLE_TEMP - 20;
                bool asic_ok = (power_management->chip_temp_avg < THROTTLE_TEMP - 10)
                               && (power_management->chip_temp2_avg < THROTTLE_TEMP - 10);

                if (vr_ok && asic_ok) {
                    if (esp_timer_get_time() - stable_since_us >= RESTORE_STABLE_US) {
                        ESP_LOGW(TAG, "Restoring user config after %d min stable: %.0f MHz / %umV (was %.0f MHz / %umV)",
                                 (int)(RESTORE_STABLE_US / 60000000LL), user_freq, user_volt, cur_freq, cur_volt);
                        nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, user_freq);
                        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, user_volt);
                        // Clear the saved baseline: the running config is the user's
                        // again, so the next overheat starts a new cycle instead of
                        // restoring a stale value.
                        nvs_config_clear_key(NVS_CONFIG_USER_FREQUENCY);
                        nvs_config_clear_key(NVS_CONFIG_USER_VOLTAGE);
                        restore_attempts++;
                        stable_since_us = 0;
                    }
                } else {
                    // Heated up again: restart the stability window.
                    stable_since_us = esp_timer_get_time();
                }
            }
        }

        uint16_t core_voltage = GLOBAL_STATE->SELF_TEST_MODULE.is_active
                                 ? GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv
                                 : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
        float asic_frequency = GLOBAL_STATE->SELF_TEST_MODULE.is_active
                                 ? GLOBAL_STATE-> DEVICE_CONFIG.family.asic.default_frequency_mhz
                                 : nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage(GLOBAL_STATE, (double) core_voltage / 1000.0);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", asic_frequency, last_asic_frequency);
            
            power_management->frequency_value = asic_frequency;
            power_management->expected_hashrate = expected_hashrate(GLOBAL_STATE);

            ASIC_set_frequency(GLOBAL_STATE);
            ASIC_set_nonce_space(GLOBAL_STATE);
            uint8_t difficulty_power = calculate_effective_asic_difficulty(power_management->expected_hashrate);
            ASIC_set_difficulty(GLOBAL_STATE, difficulty_power);
            
            last_asic_frequency = asic_frequency;
        }

        // Check for changing of overheat mode
        bool new_overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
        
        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
