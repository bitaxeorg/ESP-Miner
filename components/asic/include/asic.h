#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include "global_state.h"
#include "common.h"

uint8_t ASIC_init(GlobalState * GLOBAL_STATE);
task_result * ASIC_process_work(GlobalState * GLOBAL_STATE);
int ASIC_set_max_baud(GlobalState * GLOBAL_STATE);
void ASIC_send_work(GlobalState * GLOBAL_STATE, void * next_job);
void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask);
bool ASIC_set_frequency(GlobalState * GLOBAL_STATE, float target_frequency);
double ASIC_get_asic_job_frequency_ms(GlobalState * GLOBAL_STATE);
void ASIC_read_registers(GlobalState * GLOBAL_STATE);

#endif // ASIC_H
