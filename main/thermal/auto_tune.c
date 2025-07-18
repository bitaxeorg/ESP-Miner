#include "auto_tune.h"
#include "PID.h"
#include "esp_log.h"
#include "nvs_config.h"
#include "power_management_task.h"
#include <math.h>
#include <float.h>
#include "global_state.h"

static const char * TAG = "auto_tune";

auto_tune_settings AUTO_TUNE = {.power_limit = 20,
                                .fan_limit = 75,
                                .step_volt = 0.5,
                                .step_freq_rampup = 0.5,
                                .step_freq = 0.5,
                                .autotune_step_frequency = 0,
                                .max_voltage_asic = 1400,
                                .max_frequency_asic = 1000,
                                .max_asic_temperatur = 65,
                                .frequency = 525,
                                .voltage = 1150,
                                .auto_tune_hashrate = true};

double last_core_voltage_auto;
double last_asic_frequency_auto;
double last_hashrate_auto;
double current_hashrate_auto;
double avg_hashrate_auto;

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

void auto_tune_init(GlobalState * _GLOBAL_STATE)
{
    GLOBAL_STATE = _GLOBAL_STATE;
    AUTO_TUNE.frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, AUTO_TUNE.frequency);
    AUTO_TUNE.voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, AUTO_TUNE.voltage);
    AUTO_TUNE.power_limit = nvs_config_get_u16(NVS_CONFIG_KEY_POWER_LIMIT, AUTO_TUNE.power_limit);
    AUTO_TUNE.fan_limit = nvs_config_get_u16(NVS_CONFIG_KEY_FAN_LIMIT, AUTO_TUNE.fan_limit);
    AUTO_TUNE.max_voltage_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_VOLTAGE_ASIC, AUTO_TUNE.max_voltage_asic);
    AUTO_TUNE.max_frequency_asic = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_FREQUENCY_ASIC, AUTO_TUNE.max_frequency_asic);
    AUTO_TUNE.max_asic_temperatur = nvs_config_get_u16(NVS_CONFIG_KEY_MAX_ASIC_TEMPERATUR, AUTO_TUNE.max_asic_temperatur);
    AUTO_TUNE.auto_tune_hashrate = nvs_config_get_u16(NVS_CONFIG_KEY_AUTO_TUNE_ENABLE, AUTO_TUNE.auto_tune_hashrate);

    last_core_voltage_auto = AUTO_TUNE.voltage;
    last_asic_frequency_auto = AUTO_TUNE.frequency;
    last_hashrate_auto = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
    current_hashrate_auto = last_hashrate_auto;
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
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg < AUTO_TUNE.max_asic_temperatur;
}

bool limithit()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit || GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg >= AUTO_TUNE.max_asic_temperatur;
}

bool critical_limithit()
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg > AUTO_TUNE.max_asic_temperatur ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power >= AUTO_TUNE.power_limit + 0.2 ||
           GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc >= AUTO_TUNE.fan_limit + 5;
}

bool hashrate_decreased()
{
    return last_hashrate_auto > current_hashrate_auto;
}

static inline double clamp(double val, double min, double max) {
    return (val < min) ? min : ((val > max) ? max : val);
}

static void enforce_voltage_frequency_ratio() {
    double min_voltage = last_asic_frequency_auto * 1.75;
    double max_voltage = last_asic_frequency_auto * 2.2;

    if (last_core_voltage_auto < min_voltage) {
        last_core_voltage_auto = min_voltage;
        lastVoltageSet = true;
    } else if (last_core_voltage_auto > max_voltage) {
        last_core_voltage_auto = max_voltage;
        lastVoltageSet = false;
    }

    double min_frequency = last_core_voltage_auto / 2.2;
    double max_frequency = last_core_voltage_auto / 1.75;

    if (last_asic_frequency_auto < min_frequency) {
        last_asic_frequency_auto = min_frequency;
        lastVoltageSet = false;
    } else if (last_asic_frequency_auto > max_frequency) {
        last_asic_frequency_auto = max_frequency;
        lastVoltageSet = true;
    }
}

void increase_values() {
    if (avg_hashrate_auto > 0) {
        // Calculate step based on current and average hashrate
        double step = freq_step * (GLOBAL_STATE->SYSTEM_MODULE.current_hashrate / avg_hashrate_auto);
        last_asic_frequency_auto += step;
    }
    enforce_voltage_frequency_ratio();
}

void decrease_values() {
    if (avg_hashrate_auto > 0) {
        // Calculate step based on current and average hashrate
        double step = freq_step * (GLOBAL_STATE->SYSTEM_MODULE.current_hashrate / avg_hashrate_auto);
        last_asic_frequency_auto -= step;
    }
    enforce_voltage_frequency_ratio();
}

void switchvalue() {
    if (avg_hashrate_auto > 0) {
        // Calculate current step based on current and average hashrate
        double current_step = freq_step * (GLOBAL_STATE->SYSTEM_MODULE.current_hashrate / avg_hashrate_auto);

        if (lastVoltageSet && (last_asic_frequency_auto + current_step > last_asic_frequency_auto)) {
            last_asic_frequency_auto += current_step;
            last_core_voltage_auto -= volt_step;
        } else if (!lastVoltageSet && (last_core_voltage_auto - volt_step < last_core_voltage_auto)) {
            last_asic_frequency_auto -= current_step;
            last_core_voltage_auto += volt_step;
        }
    }

    enforce_voltage_frequency_ratio();
    lastVoltageSet = !lastVoltageSet;
}

void respectLimits() {
    last_asic_frequency_auto = clamp(last_asic_frequency_auto, MIN_FREQ, AUTO_TUNE.max_frequency_asic);
    last_core_voltage_auto = clamp(last_core_voltage_auto, MIN_VOLTAGE, AUTO_TUNE.max_voltage_asic);

    if (last_asic_frequency_auto == MIN_FREQ || last_core_voltage_auto == MIN_VOLTAGE) {
        lastVoltageSet = true;  // Assuming default voltage set to be initial value
    }
}

void dowork() {
    avg_hashrate_auto = (avg_hashrate_auto == 0) ? GLOBAL_STATE->SYSTEM_MODULE.current_hashrate :
                                                               0.99 * avg_hashrate_auto + 0.01 * GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;

    double hashrate_delta = current_hashrate_auto - last_hashrate_auto;
    double base = (last_hashrate_auto == 0) ? 1 : last_hashrate_auto;
    double step_scale = clamp(1.0 + hashrate_delta / base, 0.0, 2.0);

    freq_step = AUTO_TUNE.autotune_step_frequency * step_scale;
    volt_step = AUTO_TUNE.step_volt * step_scale;

    if (limithit()) {
        if (!critical_limithit()) {
            switchvalue();
        } else {
            last_asic_frequency_auto -= AUTO_TUNE.autotune_step_frequency * 2;
            last_core_voltage_auto -= AUTO_TUNE.step_volt * 2;
        }
    } else if (can_increase_values()) {
        increase_values();
    } else {
        decrease_values();
    }

    ESP_LOGI(TAG, "Hashrate %f Voltage %f Frequency %f", avg_hashrate_auto, last_core_voltage_auto, last_asic_frequency_auto);

    last_hashrate_auto = current_hashrate_auto;
    respectLimits();
    AUTO_TUNE.voltage = last_core_voltage_auto;
    AUTO_TUNE.frequency = last_asic_frequency_auto;
}

void auto_tune(bool pid_control_fanspeed) {
    current_hashrate_auto = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;

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
                break;  // Added this line to stop adjusting when limit is hit

            } else {
                dowork();  // Resume adjustments once limits are no longer breached
            }
            break;
    }
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