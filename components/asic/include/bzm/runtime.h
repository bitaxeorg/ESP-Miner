#ifndef BZM_RUNTIME_H
#define BZM_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/runtime_result.h"
#include "bzm/board.h"


/* Application view of the singleton chain; callbacks and context outlive calls. */
typedef struct {
    uint16_t asic_count, core_count;
    float target_frequency_mhz, actual_frequency_mhz;
    uint8_t difficulty_power;
    bool initialized;
    void *context;
    bool (*can_submit)(void *context);
    void (*report_fault)(void *context, const char *reason);
    void (*frequency_changed)(void *context, float actual_mhz);
    /* Wake the submitter without reentering the driver; called under its lock. */
    void (*request_work)(void *context);
} bzm_device_t;

static inline void bzm_device_record_frequency(bzm_device_t *device, float actual_mhz)
{
    device->actual_frequency_mhz = actual_mhz;
    if (device->frequency_changed) device->frequency_changed(device->context, actual_mhz);
}

/* Allocation placement is selected by the platform allocator. */
bool bzm_driver_state_init(bzm_device_t *state);

uint8_t bzm_driver_init(bzm_device_t *state);
void bzm_driver_set_frequency(bzm_device_t *state);
bool bzm_driver_set_difficulty(bzm_device_t *state, uint8_t difficulty_power);
int bzm_driver_set_max_baud(void);
void bzm_driver_submit_job(bzm_device_t *state, const asic_job_t *job);
bool bzm_driver_clear_work(bzm_device_t * state);
double bzm_driver_job_frequency_ms(bzm_device_t *state);
const bzm_result_t * bzm_driver_process_result(bzm_device_t * state);
float bzm_driver_read_temperature(bzm_device_t * state);
bool bzm_driver_hashrate_counter_snapshot(bzm_device_t *state,
                                   uint32_t *difficulty_one_counters,
                                   size_t counter_count);

/* Transport status used by the driver UART service. */
bool bzm_driver_has_io_fault(void);
bool bzm_driver_work_replacement_snapshot(uint32_t *generation, uint32_t *completed, bool *pending);

/* Thread-safe copies of the singleton transport's telemetry. */
bool bzm_driver_get_telemetry_snapshot(bzm_telemetry_store_t * snapshot);
/* Complete hardware startup under the driver lock. Dispatch stays closed
 * until the driver enables dispatch after success. */
bzm_bringup_outcome_t bzm_driver_start(bzm_device_t *state, const bzm_bringup_telemetry_policy_t *policy,
                                 bzm_bringup_report_t *report);
bzm_bringup_outcome_t bzm_driver_step_frequency_domains(
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report,
    float *actual_mhz);
bool bzm_driver_get_state(bzm_bringup_state_t * state);
bool bzm_driver_hold_reset(void);
/* The callback is evaluated before dispatch and before every engine write.
 * It must be non-blocking and must not call back into this driver. NULL is
 * fail-closed. */
void bzm_driver_set_dispatch_authorizer(bzm_dispatch_authorizer_t authorize, void * context);
/* Evaluated before each startup control/UART operation. */
void bzm_driver_set_operation_authorizer(bzm_dispatch_authorizer_t authorize, void * context);
/* Pump the singleton parser; returns emitted frames. */
size_t bzm_driver_poll(uint16_t timeout_ms);

void bzm_driver_service(bzm_device_t *state);

#endif // BZM_RUNTIME_H
