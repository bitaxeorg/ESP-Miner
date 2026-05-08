#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "esp_err.h"
#include "global_state.h"

void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
void SYSTEM_init_versions(GlobalState * GLOBAL_STATE);
esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg);
void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, uint8_t job_id);
void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime);

void SYSTEM_noinit_update(SystemModule * SYSTEM_MODULE);
uint64_t SYSTEM_noinit_get_total_uptime_seconds();
double SYSTEM_noinit_get_total_hashes();
double SYSTEM_noinit_get_total_log2_work();

#endif /* SYSTEM_H_ */
