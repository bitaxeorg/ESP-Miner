#include "bzm/driver.h"
#include "bzm/runtime.h"
#include "bzm/esp.h"
#include "bzm/result.h"
#include "bzm/serial_buffers.h"
#include "global_state.h"
#include "create_jobs_task.h"
#include <stdio.h>

static bool can_submit(void *context)
{
    GlobalState *state = context;
    return state->ASIC_initalized && !state->SYSTEM_MODULE.mining_paused &&
           !state->SYSTEM_MODULE.hardware_fault && !state->SYSTEM_MODULE.pools_unavailable;
}
static void report_fault(void *context, const char *reason)
{
    GlobalState *state = context;
    state->SYSTEM_MODULE.hardware_fault = true;
    snprintf(state->SYSTEM_MODULE.hardware_fault_msg,
             sizeof(state->SYSTEM_MODULE.hardware_fault_msg), "%s", reason);
    state->SYSTEM_MODULE.asic_status = state->SYSTEM_MODULE.hardware_fault_msg;
}
static void frequency_changed(void *context, float actual_mhz)
{
    GlobalState *state = context;
    state->POWER_MANAGEMENT_MODULE.actual_frequency = actual_mhz;
}
static void request_work(void *context)
{
    create_jobs_refresh(context);
}
static bzm_device_t device_view(GlobalState *state)
{
    return (bzm_device_t){
        .asic_count = state->DEVICE_CONFIG.family.asic_count,
        .core_count = state->DEVICE_CONFIG.family.asic.core_count,
        .target_frequency_mhz = state->POWER_MANAGEMENT_MODULE.frequency_value,
        .actual_frequency_mhz = state->POWER_MANAGEMENT_MODULE.actual_frequency,
        .difficulty_power = state->current_difficulty_power,
        .initialized = state->ASIC_initalized, .context = state,
        .can_submit = can_submit, .report_fault = report_fault,
        .frequency_changed = frequency_changed,
        .request_work = request_work,
    };
}
bool BZM_driver_state_init(GlobalState *state)
{
    if (!state || state->DEVICE_CONFIG.family.asic.id != BZM || !bzm_platform_esp_init()) return false;
    bzm_device_t device = device_view(state);
    return bzm_driver_state_init(&device);
}
static void device_io_task(void *context)
{
    GlobalState *state = context;
    for (;;) {
        bzm_device_t device = device_view(state);
        bzm_driver_service(&device);
        vTaskDelay(pdMS_TO_TICKS(BZM_SERIAL_SERVICE_INTERVAL_MS));
    }
}
uint8_t BZM_init(GlobalState *state)
{
    if (!BZM_driver_state_init(state)) return 0;
    bzm_device_t device = device_view(state);
    uint8_t count = bzm_driver_init(&device);
    if (!count) return 0;
    static TaskHandle_t io_task;
    if (!io_task && xTaskCreate(device_io_task, "bzm io", 4096, state, 18, &io_task) != pdPASS) {
        bzm_driver_hold_reset();
        return 0;
    }
    state->POWER_MANAGEMENT_MODULE.actual_frequency = device.actual_frequency_mhz;
    state->current_difficulty_power = device.difficulty_power;
    return count;
}
void BZM_set_frequency(GlobalState *state)
{
    if (!state) return;
    bzm_device_t device = device_view(state);
    bzm_driver_set_frequency(&device);
}
void BZM_set_difficulty(GlobalState *state, uint8_t difficulty_power)
{
    if (!state) return;
    bzm_device_t device = device_view(state);
    if (!bzm_driver_set_difficulty(&device, difficulty_power))
        report_fault(state, "BZM difficulty update failed");
}
void BZM_submit_job(GlobalState *state, const asic_job_t *job)
{
    if (!state) return;
    bzm_device_t device = device_view(state);
    bzm_driver_submit_job(&device, job);
}
bool BZM_clear_work(GlobalState *state)
{
    (void)state;
    return bzm_driver_clear_work(NULL);
}
double BZM_job_frequency_ms(GlobalState *state)
{
    if (!state) return bzm_driver_job_frequency_ms(NULL);
    bzm_device_t device = device_view(state);
    return bzm_driver_job_frequency_ms(&device);
}
task_result *BZM_process_work(GlobalState *state)
{
    if (!state) return NULL;
    bzm_device_t device = device_view(state);
    const bzm_result_t *share = bzm_driver_process_result(&device);
    static task_result result;
    return bzm_result_to_task(share, &result) ? &result : NULL;
}
float BZM_read_temperature(GlobalState *state)
{
    (void)state;
    return bzm_driver_read_temperature(NULL);
}
bool BZM_hashrate_counter_snapshot(GlobalState *state, uint32_t *counters, size_t count)
{
    if (!state) return false;
    bzm_device_t device = device_view(state);
    return bzm_driver_hashrate_counter_snapshot(&device, counters, count);
}
bool BZM_get_hashrate_measurement(GlobalState *state, uint8_t asic_nr,
                                  uint64_t *time_us, float *hashrate)
{
    if (!state) return false;
    bzm_device_t device = device_view(state);
    return bzm_driver_get_hashrate_measurement(&device, asic_nr, time_us, hashrate);
}

bzm_bringup_outcome_t BZM_start(GlobalState *state, const bzm_bringup_telemetry_policy_t *policy,
                              bzm_bringup_report_t *report)
{
    if (!BZM_driver_state_init(state)) {
        if (report) *report = (bzm_bringup_report_t){.reason = BZM_BRINGUP_REASON_INVALID_ARGUMENT};
        return BZM_BRINGUP_BAD;
    }
    bzm_device_t device = device_view(state);
    bzm_bringup_outcome_t outcome = bzm_driver_start(&device, policy, report);
    state->current_difficulty_power = device.difficulty_power;
    return outcome;
}
int BZM_set_max_baud(void) { return bzm_driver_set_max_baud(); }
bool BZM_has_io_fault(void) { return bzm_driver_has_io_fault(); }
bool BZM_work_replacement_snapshot(uint32_t *generation, uint32_t *completed, bool *pending)
{ return bzm_driver_work_replacement_snapshot(generation, completed, pending); }
bool BZM_get_telemetry_snapshot(bzm_telemetry_store_t *snapshot)
{ return bzm_driver_get_telemetry_snapshot(snapshot); }
bzm_bringup_outcome_t BZM_step_frequency_domains(
    const float target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report, float *actual_mhz)
{ return bzm_driver_step_frequency_domains(target_mhz, allow_initial_jump, report, actual_mhz); }
bool BZM_get_state(bzm_bringup_state_t *state) { return bzm_driver_get_state(state); }
bool BZM_hold_reset(void) { return bzm_driver_hold_reset(); }
void BZM_set_dispatch_authorizer(bzm_dispatch_authorizer_t authorize, void *context)
{ bzm_driver_set_dispatch_authorizer(authorize, context); }
void BZM_set_operation_authorizer(bzm_dispatch_authorizer_t authorize, void *context)
{ bzm_driver_set_operation_authorizer(authorize, context); }
size_t BZM_poll(uint16_t timeout_ms) { return bzm_driver_poll(timeout_ms); }
