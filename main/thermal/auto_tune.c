#include "auto_tune.h"
#include "PID.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include <float.h>
#include <math.h>
#define POLL_RATE 1800

static const char * TAG = "auto_tune";

auto_tune_settings AUTO_TUNE = {
    .power_limit = 20,
    .fan_limit = 75,
    .step_volt = 0.1,
    .step_freq_rampup = 0.5,
    .step_freq = 0.2,
    .autotune_step_frequency = 0,
    .max_voltage_asic = 1400,
    .max_frequency_asic = 1000,
    .max_temp_asic = 65,
    .max_temp_vr = 85,
    .frequency = 525,
    .voltage = 1150,
    .auto_tune_hashrate = false,
    .overshot_power_limit = 0.2, // watt
    .overshot_fanspeed = 5,      //%
};

#define HASHRATE_HISTORY_SIZE 30
float last_core_voltage_auto;
float last_asic_frequency_auto;
float last_hashrate_auto;
float current_hashrate_auto;
float hashrate_history[HASHRATE_HISTORY_SIZE];
int history_index = 0;
bool history_initialized = false;

bool lastVoltageSet = false;
const int waitTime = 30;
int waitCounter = 0;
float freq_step;
float volt_step;
GlobalState * GLOBAL_STATE;

#define MIN_FREQ 400
#define MIN_VOLTAGE 1000

enum TuneState
{
    sleep_before_warmup,
    warmup,
    working
};

enum TuneState state;

void update_hashrate_history(float new_value)
{
    // Initialize history if not already done
    if (!history_initialized) {
        for (int i = 0; i < HASHRATE_HISTORY_SIZE; i++) {
            hashrate_history[i] = new_value;
        }
        history_initialized = true;
    }

    // Add new value to circular buffer
    hashrate_history[history_index] = new_value;
    history_index = (history_index + 1) % HASHRATE_HISTORY_SIZE;
}

void auto_tune_init(GlobalState * _GLOBAL_STATE)
{
    GLOBAL_STATE = _GLOBAL_STATE;
    AUTO_TUNE.frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    AUTO_TUNE.voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
    AUTO_TUNE.power_limit = nvs_config_get_u16(NVS_CONFIG_KEY_POWER_LIMIT);
    AUTO_TUNE.fan_limit = nvs_config_get_u16(NVS_CONFIG_KEY_FAN_LIMIT);
    AUTO_TUNE.max_voltage_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_VOLTAGE_ASIC);
    AUTO_TUNE.max_frequency_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_FREQUENCY_ASIC);
    AUTO_TUNE.max_temp_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_TEMP_ASIC);
    AUTO_TUNE.auto_tune_hashrate = nvs_config_get_bool(NVS_CONFIG_KEY_AUTO_TUNE_ENABLE);
    AUTO_TUNE.overshot_power_limit = nvs_config_get_float(NVS_CONFIG_KEY_OVERSHOT_POWER_LIMIT);
    AUTO_TUNE.overshot_fanspeed = nvs_config_get_u16(NVS_CONFIG_KEY_OVERSHOT_FAN_LIMIT);
    AUTO_TUNE.max_temp_vr = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_TEMP_VR);

    last_core_voltage_auto = AUTO_TUNE.voltage;
    last_asic_frequency_auto = AUTO_TUNE.frequency;
    last_hashrate_auto = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
    current_hashrate_auto = last_hashrate_auto;

    // Initialize hashrate history
    update_hashrate_history(last_hashrate_auto);

    state = sleep_before_warmup;
    waitCounter = 45 * 1000 / POLL_RATE;
}

bool waitForStartUp()
{
    return current_hashrate_auto > 0 && waitCounter <= 0;
}

bool can_increase_values()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc < AUTO_TUNE.fan_limit &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power < AUTO_TUNE.power_limit &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg < AUTO_TUNE.max_temp_asic &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp2_avg < AUTO_TUNE.max_temp_asic &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp < AUTO_TUNE.max_temp_vr;
}

bool limithit()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc > AUTO_TUNE.fan_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power > AUTO_TUNE.power_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_temp_asic ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp2_avg > AUTO_TUNE.max_temp_asic;
           
}

bool critical_limithit()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_temp_asic ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp2_avg > AUTO_TUNE.max_temp_asic ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit + AUTO_TUNE.overshot_power_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit + AUTO_TUNE.overshot_fanspeed || 
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp > AUTO_TUNE.max_temp_vr;
}

bool hashrate_decreased()
{
    return last_hashrate_auto > current_hashrate_auto;
}



bool hashrate_increased_since_last_set()
{
    if (!history_initialized) {
        return false; // Not enough data yet
    }

    int last_set_index = history_index;

    // Check if hashrate increased since that point
    float last_value = hashrate_history[last_set_index];
    bool increased = false;

    for (int i = 1; i < HASHRATE_HISTORY_SIZE; i++) {
        int idx = (last_set_index + i) % HASHRATE_HISTORY_SIZE;
        if (hashrate_history[idx] > last_value) {
            increased = true;
            break;
        }
    }

    return increased;
}

static inline float clamp(float val, float min, float max)
{
    return (val < min) ? min : ((val > max) ? max : val);
}

void increase_values()
{
    if (!lastVoltageSet) {
        last_asic_frequency_auto += freq_step;
    } else {
        last_core_voltage_auto += volt_step;
    }
    //enforce_voltage_frequency_ratio();
}

void respectLimits()
{
    last_asic_frequency_auto = clamp(last_asic_frequency_auto, MIN_FREQ, AUTO_TUNE.max_frequency_asic);
    last_core_voltage_auto = clamp(last_core_voltage_auto, MIN_VOLTAGE, AUTO_TUNE.max_voltage_asic);

    if (last_asic_frequency_auto == MIN_FREQ || last_core_voltage_auto == MIN_VOLTAGE) {
        lastVoltageSet = true; // Assuming default voltage set to be initial value
    }
}

void dowork()
{
    freq_step = AUTO_TUNE.autotune_step_frequency;
    volt_step = AUTO_TUNE.step_volt;

    // Update hashrate history with current value
    

    // Check if hashrate increased since last voltage/frequency set
    bool hashrate_increased = hashrate_increased_since_last_set();

    // If hashrate didn't increase, switch the setting
    if (!hashrate_increased) {
        lastVoltageSet = !lastVoltageSet;
    }

    if (critical_limithit()) {
        last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency;
        last_core_voltage_auto -= AUTO_TUNE.step_volt;
    } else if (can_increase_values()) {
        increase_values();
    }

    ESP_LOGI(TAG, "Hashrate %f Voltage %f Frequency %f", current_hashrate_auto, last_core_voltage_auto, last_asic_frequency_auto);

    respectLimits();
    AUTO_TUNE.voltage = last_core_voltage_auto;
    AUTO_TUNE.frequency = last_asic_frequency_auto;
}

void auto_tune()
{
    current_hashrate_auto = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
    update_hashrate_history(current_hashrate_auto);

    switch (state) {
    case sleep_before_warmup:
        if (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg == -1) {
            break;
        }

        if (waitCounter-- > 0) {
            ESP_LOGI(TAG, "state sleep_bevor_warmup %i", waitCounter);
            break;
        }

        if (waitForStartUp()) {
            state = warmup;
        }
        break;

    case warmup:
        AUTO_TUNE.autotune_step_frequency = AUTO_TUNE.step_freq_rampup;
        dowork();
        if (limithit()) {
            AUTO_TUNE.autotune_step_frequency = AUTO_TUNE.step_freq;
            state = working;
        }
        break;

    case working:
        if (limithit() && !critical_limithit()) {
            break; // Added this line to stop adjusting when limit is hit

        } else {
            dowork(); // Resume adjustments once limits are no longer breached
        }
        break;
    }
    last_hashrate_auto = current_hashrate_auto;
}

float auto_tune_get_frequency()
{
    return AUTO_TUNE.frequency;
}

float auto_tune_get_voltage()
{
    return AUTO_TUNE.voltage;
}

bool auto_tune_get_auto_tune_hashrate()
{
    return AUTO_TUNE.auto_tune_hashrate;
}

void auto_tune_set_auto_tune_hashrate(bool enable)
{
    AUTO_TUNE.auto_tune_hashrate = enable;
}