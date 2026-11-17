#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdbool.h>
#include "esp_err.h"
#include "miner_job.h"
#include "stratum_api.h"

typedef struct GlobalState GlobalState;
typedef struct SystemModule SystemModule;

void SYSTEM_check_firmware_migration(void);
void SYSTEM_reset_custom_www(void);
void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
void SYSTEM_init_versions(GlobalState * GLOBAL_STATE);
void SYSTEM_init_partitions(GlobalState * GLOBAL_STATE);
esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);

void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, bool is_block);
void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime);

// Invalidate active ASIC jobs and reset hashrate measurements on pool disconnection/reconnect.
void SYSTEM_clean_jobs_queue(GlobalState * GLOBAL_STATE);

// Reset decoded coinbase UI fields (scriptsig, coinbase values, outputs, block signals).
// Note: block_height is intentionally NOT reset here; it is preserved as the "last known good"
// network height so the UI, screen, and BAP do not flicker or lose context on transient disconnects.
void SYSTEM_reset_coinbase_ui_state(GlobalState * GLOBAL_STATE);
void SYSTEM_decode_and_apply_coinbase(GlobalState * GLOBAL_STATE, const miner_job_t * job);

void SYSTEM_noinit_update(SystemModule * SYSTEM_MODULE);
uint64_t SYSTEM_noinit_get_total_uptime_seconds();
double SYSTEM_noinit_get_total_hashes();
double SYSTEM_noinit_get_total_log2_work();
void SYSTEM_load_pool_from_nvs(GlobalState * GLOBAL_STATE, int i);
void SYSTEM_reload_pool_config(GlobalState * GLOBAL_STATE);

#endif /* SYSTEM_H_ */
