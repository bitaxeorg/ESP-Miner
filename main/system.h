#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "esp_err.h"
#include "global_state.h"

#ifdef CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE
    #define STRATUM_EXTRANONCE_SUBSCRIBE 1
#else
    #define STRATUM_EXTRANONCE_SUBSCRIBE 0
#endif

#ifdef CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE
    #define FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE 1
#else
    #define FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE 0
#endif

void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg);
void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double found_diff, uint8_t job_id);
void SYSTEM_notify_mining_started(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime);

#endif /* SYSTEM_H_ */
