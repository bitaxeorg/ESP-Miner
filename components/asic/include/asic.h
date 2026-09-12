#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include <stdint.h>
#include "asic_job.h"

typedef struct GlobalState GlobalState;
typedef struct task_result task_result;
typedef struct bm_job bm_job;

typedef struct {
    uint64_t time_us;
    float hashrate;
} asic_domain_measurement_t;

uint8_t ASIC_init(GlobalState * GLOBAL_STATE);
task_result * ASIC_process_work(GlobalState * GLOBAL_STATE);
int ASIC_set_max_baud(GlobalState * GLOBAL_STATE);
void ASIC_send_work(GlobalState * GLOBAL_STATE, bm_job * next_job);
/* Borrow common work for this call; the Bitmain adapter retains its own copy. */
void ASIC_send_job(GlobalState *state, const asic_job_t *job);
void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask);
void ASIC_set_frequency(GlobalState * GLOBAL_STATE);
void ASIC_set_nonce_space(GlobalState * GLOBAL_STATE);
double ASIC_get_asic_job_frequency_ms(GlobalState * GLOBAL_STATE);
void ASIC_read_registers(GlobalState * GLOBAL_STATE);
esp_err_t ASIC_get_domain_measurement(GlobalState * GLOBAL_STATE, uint8_t asic_nr,
                                      uint8_t domain_nr, asic_domain_measurement_t * measurement);

#endif // ASIC_H
