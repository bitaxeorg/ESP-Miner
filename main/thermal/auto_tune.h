#ifndef AUTO_TUNE_H_
#define AUTO_TUNE_H_
#include <stdbool.h>
#include <stdint.h>
#include "global_state.h"

typedef struct
{
    double power_limit;
    uint16_t fan_limit;
    double step_volt;
    double step_freq_rampup;
    double step_freq;
    double autotune_step_frequency;
    uint8_t autotune_read_tick;
    uint16_t max_voltage_asic;
    uint16_t max_frequency_asic;
    uint8_t max_temp_asic;
    uint16_t max_temp_vr;
    double frequency;
    double voltage;
    bool auto_tune_hashrate;
    double overshot_power_limit;
    uint16_t overshot_fanspeed;
    double vf_ratio_max;
    double vf_ratio_min;
} auto_tune_settings;

extern auto_tune_settings AUTO_TUNE;
void auto_tune_init(GlobalState * gs);
void auto_tune(bool pid_control_fanspeed);
double auto_tune_get_frequency();
double auto_tune_get_voltage();
bool auto_tune_get_auto_tune_hashrate();
void auto_tune_set_auto_tune_hashrate(bool enable);
#endif