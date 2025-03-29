#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include "global_state.h"
#include "common.h"

#define BITAXE_MAX_ASIC_COUNT 1
#define BITAXE_ULTRA_ASIC_COUNT 1
#define BITAXE_SUPRA_ASIC_COUNT 1
#define BITAXE_GAMMA_ASIC_COUNT 1
#define BITAXE_GAMMATURBO_ASIC_COUNT 2

uint8_t ASIC_init(GlobalState * GLOBAL_STATE);
uint8_t ASIC_get_asic_count(GlobalState * GLOBAL_STATE);
uint16_t ASIC_get_small_core_count(GlobalState * GLOBAL_STATE);
task_result * ASIC_process_work(GlobalState * GLOBAL_STATE);
int ASIC_set_max_baud(GlobalState * GLOBAL_STATE);
void ASIC_set_job_difficulty_mask(GlobalState * GLOBAL_STATE, uint8_t mask);
void ASIC_send_work(GlobalState * GLOBAL_STATE, void * next_job);
void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask);
bool ASIC_set_frequency(GlobalState * GLOBAL_STATE, float target_frequency);
esp_err_t ASIC_set_device_model(GlobalState * GLOBAL_STATE);

#endif // ASIC_H
