#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "esp_err.h"


void SYSTEM_init_system();
esp_err_t SYSTEM_init_peripherals();
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


void SYSTEM_notify_accepted_share();
void SYSTEM_notify_rejected_share(char * error_msg);
void SYSTEM_notify_found_nonce( double found_diff, uint8_t job_id);
void SYSTEM_notify_mining_started();
void SYSTEM_notify_new_ntime( uint32_t ntime);

#endif /* SYSTEM_H_ */
