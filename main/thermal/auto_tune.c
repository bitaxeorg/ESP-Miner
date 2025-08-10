#include "auto_tune.h"
#include "PID.h"
#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include "power_management_task.h"
#include <float.h>
#include <math.h>

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
    .max_asic_temperatur = 65,
    .max_vr_temperatur = 85,
    .frequency = 525,
    .voltage = 1150,
    .auto_tune_hashrate = false,
    .overshot_power_limit = 0.2, // watt
    .overshot_fanspeed = 5,      //%
    .vf_ratio_max = 2.2,
    .vf_ratio_min = 1.76,
};

#define HASHRATE_HISTORY_SIZE 30
double last_core_voltage_auto;
double last_asic_frequency_auto;
double last_hashrate_auto;
double current_hashrate_auto;
double hashrate_history[HASHRATE_HISTORY_SIZE];
int history_index = 0;
bool history_initialized = false;

bool lastVoltageSet = false;
const int waitTime = 30;
int waitCounter = 0;
double freq_step;
double volt_step;
GlobalState * GLOBAL_STATE;

#define INIT_FREQ 525
#define INIT_VOLTAGE 1150
#define MIN_FREQ 401
#define MIN_VOLTAGE 1001

enum TuneState
{
    sleep_before_warmup,
    warmup,
    working
};

enum TuneState state;

void update_hashrate_history(double new_value)
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
    AUTO_TUNE.frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQUENCY, AUTO_TUNE.frequency);
    AUTO_TUNE.voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, AUTO_TUNE.voltage);
    AUTO_TUNE.power_limit = nvs_config_get_u16(NVS_CONFIG_KEY_POWER_LIMIT, AUTO_TUNE.power_limit);
    AUTO_TUNE.fan_limit = nvs_config_get_u16(NVS_CONFIG_KEY_FAN_LIMIT, AUTO_TUNE.fan_limit);
    AUTO_TUNE.max_voltage_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_VOLTAGE_ASIC, AUTO_TUNE.max_voltage_asic);
    AUTO_TUNE.max_frequency_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_FREQUENCY_ASIC, AUTO_TUNE.max_frequency_asic);
    AUTO_TUNE.max_asic_temperatur = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_ASIC_TEMPERATUR, AUTO_TUNE.max_asic_temperatur);
    AUTO_TUNE.auto_tune_hashrate = nvs_config_get_bool(NVS_CONFIG_KEY_AUTO_TUNE_ENABLE, AUTO_TUNE.auto_tune_hashrate);
    AUTO_TUNE.overshot_power_limit = nvs_config_get_double(NVS_CONFIG_KEY_OVERSHOT_POWER_LIMIT, AUTO_TUNE.overshot_power_limit);
    AUTO_TUNE.overshot_fanspeed = nvs_config_get_u16(NVS_CONFIG_KEY_OVERSHOT_FAN_LIMIT, AUTO_TUNE.overshot_fanspeed);
    AUTO_TUNE.vf_ratio_max = nvs_config_get_double(NVS_CONFIG_KEY_VF_RATIO_MAX, AUTO_TUNE.vf_ratio_max);
    AUTO_TUNE.vf_ratio_min = nvs_config_get_double(NVS_CONFIG_KEY_VF_RATIO_MIN, AUTO_TUNE.vf_ratio_min);
    AUTO_TUNE.max_vr_temperatur = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_VR_TEMPERATUR, AUTO_TUNE.max_vr_temperatur);

    last_core_voltage_auto = AUTO_TUNE.voltage;
    last_asic_frequency_auto = AUTO_TUNE.frequency;
    last_hashrate_auto = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
    current_hashrate_auto = last_hashrate_auto;

    // Initialize hashrate history
    update_hashrate_history(last_hashrate_auto);

    state = sleep_before_warmup;
    waitCounter = 45 * 1000 / POLL_RATE;
}

bool waitForStartUp(bool pid_control_fanspeed)
{
    return current_hashrate_auto > 0 && pid_control_fanspeed && waitCounter <= 0;
}

bool can_increase_values()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc < AUTO_TUNE.fan_limit &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power < AUTO_TUNE.power_limit &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg < AUTO_TUNE.max_asic_temperatur &&
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp < AUTO_TUNE.max_vr_temperatur;
}

bool limithit()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc > AUTO_TUNE.fan_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power > AUTO_TUNE.power_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_asic_temperatur;
           
}

bool critical_limithit()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_asic_temperatur ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit + AUTO_TUNE.overshot_power_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit + AUTO_TUNE.overshot_fanspeed || 
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp > AUTO_TUNE.max_vr_temperatur;
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
    double last_value = hashrate_history[last_set_index];
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

static inline double clamp(double val, double min, double max)
{
    return (val < min) ? min : ((val > max) ? max : val);
}

static void enforce_voltage_frequency_ratio()
{
    double min_voltage = last_asic_frequency_auto * AUTO_TUNE.vf_ratio_min;
    double max_voltage = last_asic_frequency_auto * AUTO_TUNE.vf_ratio_max;

    if (last_core_voltage_auto < min_voltage) {
        last_core_voltage_auto = min_voltage;
        lastVoltageSet = true;
    } else if (last_core_voltage_auto > max_voltage) {
        last_core_voltage_auto = max_voltage;
        lastVoltageSet = false;
    }

    double min_frequency = last_core_voltage_auto / AUTO_TUNE.vf_ratio_max;
    double max_frequency = last_core_voltage_auto / AUTO_TUNE.vf_ratio_min;

    if (last_asic_frequency_auto < min_frequency) {
        last_asic_frequency_auto = min_frequency;
        lastVoltageSet = false;
    } else if (last_asic_frequency_auto > max_frequency) {
        last_asic_frequency_auto = max_frequency;
        lastVoltageSet = true;
    }
}

void increase_values()
{
    if (!lastVoltageSet) {
        last_asic_frequency_auto += freq_step;
    } else {
        last_core_voltage_auto += volt_step;
    }
    enforce_voltage_frequency_ratio();
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

void auto_tune(bool pid_control_fanspeed)
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

        if (waitForStartUp(pid_control_fanspeed)) {
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

double auto_tune_get_frequency()
{
    return AUTO_TUNE.frequency;
}

double auto_tune_get_voltage()
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