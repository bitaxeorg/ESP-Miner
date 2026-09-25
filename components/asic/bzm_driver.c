#include "bzm/telemetry_internal.h"
#include "bzm/driver.h"
#include "bzm/board.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>

#include "bzm/bridge.h"
#include "bzm/balanced_ramp.h"
#include "bzm/bringup.h"
#include "bzm/dispatch_gate.h"
#include "bzm/lease_guard_internal.h"
#include "bzm/reactor.h"
#include "bzm/result.h"
#include "bzm/result_dedup.h"
#include "bzm/registers.h"
#include "bzm/transport.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "mining.h"
#include "bzm/serial.h"

static const char * TAG = "bzm";

#define BZM_FAST_JOB_INTERVAL_MS 10.0
#define BZM_STEADY_JOB_INTERVAL_MS 100.0

/* A 36-bit filter reports difficulty-16 results. Derive local validation
 * and hashrate credits from the same setting programmed into the engines. */
#define BZM_RESULT_LEAD_ZEROS 36U
#define BZM_RESULT_DIFFICULTY (UINT64_C(1) << (BZM_RESULT_LEAD_ZEROS - 32U))
#define BZM_DISPATCH_GAP_US 250U

typedef struct {
    bzm_job_store_t job_store;
    atomic_uint work_epoch;
    bzm_reactor_t reactor;
    bzm_serial_transport_t transport;
    pthread_mutex_t reactor_lock;
    bool initialized;
    atomic_bool io_failed;
    bool driver_transport_ready;
    bool driver_lease_io_ok;
    bzm_lease_guard_schedule_t driver_lease_schedule;
    bzm_dispatch_authorizer_t driver_operation_authorize;
    void *driver_operation_authorize_context;
    bzm_bringup_state_t driver_bringup;
    bzm_balanced_ramp_report_t driver_balanced_ramp;
    bzm_bringup_telemetry_policy_t driver_ramp_telemetry_policy;
    bzm_telemetry_store_t driver_batch_telemetry;
    bzm_dispatch_gate_t driver_dispatch_gate;
    float last_temperature;
    int64_t last_temperature_us;
    atomic_uint_fast64_t
        hashrate_difficulty_one_counters[BZM_MAX_ASIC_COUNT];
    atomic_uint_fast32_t work_replacement_generation;
    uint32_t work_replacement_completed; // Protected by REACTOR_LOCK.
    uint16_t work_replacement_remaining;
    uint16_t fast_dispatch_remaining;
    bzm_result_dedup_t result_dedup;
} bzm_driver_state_t;

static bzm_driver_state_t *BZM_STATE;

#define REACTOR (BZM_STATE->reactor)
#define TRANSPORT (BZM_STATE->transport)
#define REACTOR_LOCK (BZM_STATE->reactor_lock)
#define INITIALIZED (BZM_STATE->initialized)
#define DRIVER_TRANSPORT_READY (BZM_STATE->driver_transport_ready)
#define DRIVER_LEASE_IO_OK (BZM_STATE->driver_lease_io_ok)
#define DRIVER_LEASE_SCHEDULE (BZM_STATE->driver_lease_schedule)
#define DRIVER_OPERATION_AUTHORIZE (BZM_STATE->driver_operation_authorize)
#define DRIVER_OPERATION_AUTHORIZE_CONTEXT (BZM_STATE->driver_operation_authorize_context)
#define DRIVER_BRINGUP (BZM_STATE->driver_bringup)
#define DRIVER_BALANCED_RAMP (BZM_STATE->driver_balanced_ramp)
#define DRIVER_RAMP_TELEMETRY_POLICY (BZM_STATE->driver_ramp_telemetry_policy)
#define DRIVER_BATCH_TELEMETRY (BZM_STATE->driver_batch_telemetry)
#define DRIVER_DISPATCH_GATE (BZM_STATE->driver_dispatch_gate)
#define LAST_TEMPERATURE (BZM_STATE->last_temperature)
#define LAST_TEMPERATURE_US (BZM_STATE->last_temperature_us)
#define HASHRATE_DIFFICULTY_ONE_COUNTERS (BZM_STATE->hashrate_difficulty_one_counters)
#define WORK_REPLACEMENT_GENERATION (BZM_STATE->work_replacement_generation)
#define FAST_DISPATCH_REMAINING (BZM_STATE->fast_dispatch_remaining)
#define RESULT_DEDUP (BZM_STATE->result_dedup)

static void driver_sleep(void *context, uint32_t delay_ms);

bool BZM_driver_state_init(GlobalState *state)
{
    if (state == NULL || state->DEVICE_CONFIG.family.asic.id != BZM) {
        return false;
    }
    if (BZM_STATE != NULL) return true;
    if (!esp_psram_is_initialized()) return false;

    bzm_driver_state_t *allocated = heap_caps_calloc(
        1, sizeof(*allocated), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (allocated == NULL) {
        ESP_LOGE(TAG, "Unable to allocate Bonanza driver state in PSRAM");
        return false;
    }
    if (pthread_mutex_init(&allocated->reactor_lock, NULL) != 0) {
        heap_caps_free(allocated);
        return false;
    }
    allocated->last_temperature = -1.0f;
    atomic_init(&allocated->work_epoch, 0);
    atomic_init(&allocated->io_failed, false);
    atomic_init(&allocated->work_replacement_generation, 0);
    for (size_t asic = 0; asic < BZM_MAX_ASIC_COUNT; ++asic) {
        atomic_init(&allocated->hashrate_difficulty_one_counters[asic], 0);
    }

    if (!bzm_job_store_init_with_caps(&allocated->job_store,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {
        pthread_mutex_destroy(&allocated->reactor_lock);
        heap_caps_free(allocated);
        return false;
    }
    BZM_STATE = allocated;
    ESP_LOGI(TAG, "Allocated %u bytes of Bonanza driver state in PSRAM",
             (unsigned)sizeof(*allocated));
    return true;
}

static void reset_result_dedup(void)
{
    memset(&RESULT_DEDUP, 0, sizeof(RESULT_DEDUP));
}

static void start_work_replacement_locked(uint16_t remaining)
{
    FAST_DISPATCH_REMAINING = remaining;
    BZM_STATE->work_replacement_remaining = remaining;
    if (remaining != 0) {
        atomic_fetch_add_explicit(
            &WORK_REPLACEMENT_GENERATION, 1, memory_order_relaxed);
    }
}

static bool result_is_duplicate(const bzm_result_t *result)
{
    return result->job_valid &&
           bzm_result_is_duplicate(&RESULT_DEDUP, &result->job, result);
}

static void driver_report(bzm_bringup_report_t * report, bzm_bringup_reason_t reason)
{
    if (report != NULL) {
        *report = (bzm_bringup_report_t){
            .reason = reason,
        };
    }
}

int BZM_set_max_baud(void)
{
    return BZM_SERIAL_set_baud(BZM_BAUD_RATE) == ESP_OK ? BZM_BAUD_RATE : 0;
}

static bool BZM_send_work(GlobalState * state, const asic_job_t * template)
{
    (void) state;
    if (BZM_STATE == NULL || !INITIALIZED || template == NULL) {
        return false;
    }

    if (DRIVER_TRANSPORT_READY) {
        pthread_mutex_lock(&REACTOR_LOCK);
        bool running = DRIVER_BRINGUP.running;
        bzm_dispatch_gate_t gate = DRIVER_DISPATCH_GATE;
        pthread_mutex_unlock(&REACTOR_LOCK);
        if (!running || !bzm_dispatch_gate_is_authorized(&gate))
            return false;
    }

    pthread_mutex_lock(&REACTOR_LOCK);
    bool results_were_quarantined =
        bzm_reactor_results_quarantined(&REACTOR);

    bzm_work_t assigned_work;
    bzm_assign_status_t status =
        bzm_reactor_assign(&REACTOR, template, &assigned_work);
    if (status == BZM_ASSIGN_FLUSH_REQUIRED) {
        if (!bzm_reactor_begin_flush(&REACTOR)) {
            pthread_mutex_unlock(&REACTOR_LOCK);
            atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
            ESP_LOGE(TAG, "Unable to complete BZM flush barrier");
            return false;
        }
        bzm_reactor_finish_flush(&REACTOR);
        // Hardware sequence reuse does not make an already seen share new.
        start_work_replacement_locked(REACTOR.config.engine_count);
        results_were_quarantined = true;
        status = bzm_reactor_assign(&REACTOR, template, &assigned_work);
    }
    bool fast_dispatch_complete = status == BZM_ASSIGN_OK &&
        FAST_DISPATCH_REMAINING == 1;
    if (status == BZM_ASSIGN_OK && BZM_STATE->work_replacement_remaining != 0 &&
        --BZM_STATE->work_replacement_remaining == 0)
        BZM_STATE->work_replacement_completed = (uint32_t)atomic_load_explicit(
            &WORK_REPLACEMENT_GENERATION, memory_order_relaxed);
    if (status == BZM_ASSIGN_OK && FAST_DISPATCH_REMAINING != 0)
        --FAST_DISPATCH_REMAINING;
    if (status == BZM_ASSIGN_OK && results_were_quarantined &&
        !bzm_reactor_results_quarantined(&REACTOR)) {
        /* A clean Stratum job makes every pre-flush result unsubmitable.
         * Drain anything queued during the bounded replacement rotation
         * before normal attribution resumes. */
        bzm_serial_discard_pending_results(&TRANSPORT);
    }
    /* Pause and other board transitions withdraw dispatch permission
     * asynchronously so a long engine rotation stops at its next checkpoint.
     * The reactor reports that deliberate cancellation as a transport error;
     * keep returning false to stop the caller, but do not turn the requested
     * safe-off transition into a runtime hardware fault. */
    bool dispatch_cancelled = status != BZM_ASSIGN_OK &&
        DRIVER_TRANSPORT_READY &&
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE);
    pthread_mutex_unlock(&REACTOR_LOCK);

    if (status != BZM_ASSIGN_OK) {
        if (dispatch_cancelled) {
            ESP_LOGI(TAG,
                     "BZM work assignment cancelled after dispatch authorization was withdrawn");
            return false;
        }
        atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
        ESP_LOGE(TAG, "BZM work assignment failed (%d)", status);
        return false;
    }
    if (fast_dispatch_complete) {
        ESP_LOGI(TAG,
                 "BZM independent engine rotation complete; steady refresh interval %.0f ms",
                 BZM_STEADY_JOB_INTERVAL_MS);
    }
    return true;
}

bool BZM_clear_work(GlobalState * state)
{
    if (BZM_STATE == NULL) return true;
    atomic_fetch_add_explicit(&BZM_STATE->work_epoch, 1, memory_order_acq_rel);
    (void) state;
    pthread_mutex_lock(&REACTOR_LOCK);
    bool cleared = !INITIALIZED || bzm_reactor_invalidate_work(&REACTOR);
    if (cleared) {
        reset_result_dedup();
        /* Refresh every engine quickly after a clean job. These newer jobs
         * also count toward any startup/PLL replacement already in progress;
         * frequent pool notifications must not restart that synchronization. */
        if (INITIALIZED) FAST_DISPATCH_REMAINING = REACTOR.config.engine_count;
    }
    pthread_mutex_unlock(&REACTOR_LOCK);
    if (!cleared) {
        atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
        ESP_LOGE(TAG, "BZM clean-job barrier failed");
    }
    return cleared;
}

double BZM_job_frequency_ms(GlobalState *state)
{
    if (BZM_STATE == NULL) return BZM_FAST_JOB_INTERVAL_MS;
    (void)state;
    pthread_mutex_lock(&REACTOR_LOCK);
    bool fast_dispatch = INITIALIZED && FAST_DISPATCH_REMAINING != 0;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return fast_dispatch ? BZM_FAST_JOB_INTERVAL_MS
                         : BZM_STEADY_JOB_INTERVAL_MS;
}

static bzm_result_t * BZM_process_result(GlobalState * state)
{
    if (BZM_STATE == NULL) return NULL;
    (void) state;
    if (!INITIALIZED || (DRIVER_TRANSPORT_READY && !DRIVER_BRINGUP.running)) {
        return NULL;
    }

    bzm_raw_result_t raw;
    static bzm_result_t event;
    pthread_mutex_lock(&REACTOR_LOCK);
    bool received = bzm_serial_read_result(&TRANSPORT, &raw, 20);
    if (received && bzm_reactor_results_quarantined(&REACTOR)) {
        pthread_mutex_unlock(&REACTOR_LOCK);
        vTaskDelay(1);
        return NULL;
    }
    bool nonce_frame = received && bzm_raw_result_has_valid_nonce(&raw);
    bool mapped = nonce_frame && bzm_reactor_map_result(&REACTOR, &raw, &event);
    bool duplicate = mapped && result_is_duplicate(&event);
    pthread_mutex_unlock(&REACTOR_LOCK);
    if (duplicate) {
        vTaskDelay(1);
        return NULL;
    }
    /* The lower-priority Stratum task must be able to acquire REACTOR_LOCK
     * to retire a disconnected pool. Repeated timed reads followed by an
     * immediate lock reacquisition otherwise starve that transition when
     * all arriving results are stale. Block outside the lock for one tick;
     * taskYIELD alone cannot schedule a lower-priority waiter. */
    if (!mapped)
        vTaskDelay(1);
    return mapped ? &event : NULL;
}

bool BZM_hashrate_counter_snapshot(GlobalState *state,
                                   uint32_t *difficulty_one_counters,
                                   size_t counter_count)
{
    if (BZM_STATE == NULL || state == NULL || difficulty_one_counters == NULL ||
        counter_count < state->DEVICE_CONFIG.family.asic_count ||
        state->DEVICE_CONFIG.family.asic_count > BZM_MAX_ASIC_COUNT) {
        return false;
    }
    for (size_t i = 0; i < state->DEVICE_CONFIG.family.asic_count; ++i) {
        difficulty_one_counters[i] = (uint32_t)atomic_load_explicit(
            &HASHRATE_DIFFICULTY_ONE_COUNTERS[i], memory_order_relaxed);
    }
    return true;
}

bool BZM_work_replacement_snapshot(uint32_t *generation, uint32_t *completed, bool *pending)
{
    if (generation == NULL || completed == NULL || pending == NULL) return false;
    pthread_mutex_lock(&REACTOR_LOCK);
    *pending = INITIALIZED && BZM_STATE->work_replacement_remaining != 0;
    *generation = (uint32_t)atomic_load_explicit(
        &WORK_REPLACEMENT_GENERATION, memory_order_relaxed);
    *completed = BZM_STATE->work_replacement_completed;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return true;
}

float BZM_read_temperature(GlobalState * state)
{
    (void) state;
    if (BZM_STATE == NULL || !INITIALIZED || TRANSPORT.asic_count == 0)
        return -1.0f;

    int64_t now = esp_timer_get_time();
    if (LAST_TEMPERATURE_US != 0 && now - LAST_TEMPERATURE_US < 2000000) {
        return LAST_TEMPERATURE;
    }

    bzm_telemetry_store_t snapshot;
    pthread_mutex_lock(&REACTOR_LOCK);
    const bool available =
        bzm_serial_get_telemetry_snapshot(&TRANSPORT, &snapshot);
    pthread_mutex_unlock(&REACTOR_LOCK);

    float hottest_c = -1.0f;
    if (available &&
        bzm_telemetry_max_temperature(
            &snapshot, (uint64_t)now,
            BZM_TELEMETRY_MAX_AGE_US,
            &hottest_c)) {
        LAST_TEMPERATURE = hottest_c;
    }
    LAST_TEMPERATURE_US = now;
    return LAST_TEMPERATURE;
}

bool BZM_get_telemetry_snapshot(bzm_telemetry_store_t * snapshot)
{
    return bzm_serial_get_telemetry_snapshot(&TRANSPORT, snapshot);
}

static bool driver_lease_renew(void * context)
{
    (void) context;
    if (DRIVER_OPERATION_AUTHORIZE == NULL || !DRIVER_OPERATION_AUTHORIZE(DRIVER_OPERATION_AUTHORIZE_CONTEXT)) {
        return false;
    }
    bzm_bridge_safety_status_t status;
    bool renewed = BZM_bridge_safety_heartbeat(&status) == ESP_OK && bzm_lease_guard_status_is_controlled(&status);
    if (renewed) {
        DRIVER_LEASE_SCHEDULE.renewed = true;
        DRIVER_LEASE_SCHEDULE.last_renewal_ms = (uint64_t) (esp_timer_get_time() / 1000);
    }
    return renewed;
}

static bool driver_lease_check(void)
{
    if (!DRIVER_LEASE_IO_OK || !driver_lease_renew(NULL)) {
        DRIVER_LEASE_IO_OK = false;
        return false;
    }
    return true;
}

static bool driver_mining_lease_renew(void * context)
{
    (void) context;
    bzm_bridge_safety_status_t status;
    bool renewed = BZM_bridge_safety_heartbeat(&status) == ESP_OK && bzm_lease_guard_status_is_controlled(&status);
    if (renewed) {
        DRIVER_LEASE_SCHEDULE.renewed = true;
        DRIVER_LEASE_SCHEDULE.last_renewal_ms = (uint64_t) (esp_timer_get_time() / 1000);
    }
    return renewed;
}

static bool driver_mining_lease_service_due(void)
{
    uint64_t current_ms = (uint64_t) (esp_timer_get_time() / 1000);
    bool dispatch_authorized = DRIVER_TRANSPORT_READY && DRIVER_BRINGUP.running &&
                               bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE);
    if (!DRIVER_LEASE_IO_OK ||
        !bzm_lease_guard_service_authorized(&DRIVER_LEASE_SCHEDULE, dispatch_authorized, current_ms,
                                            BZM_LEASE_GUARD_MAX_DELAY_CHUNK_MS, driver_mining_lease_renew, NULL)) {
        DRIVER_LEASE_IO_OK = false;
        return false;
    }
    return true;
}

static bool driver_live_mining_check(void)
{
    if (!DRIVER_LEASE_IO_OK || !DRIVER_TRANSPORT_READY ||
        !DRIVER_BRINGUP.running ||
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE) ||
        !driver_mining_lease_service_due()) {
        DRIVER_LEASE_IO_OK = false;
        return false;
    }
    return true;
}

static bool driver_live_write_u32(void *context, uint8_t asic_id,
                                  uint16_t engine_id, uint8_t offset,
                                  uint32_t value)
{
    if (!driver_live_mining_check()) return false;
    uint8_t bytes[4] = {
        value & 0xff,
        (value >> 8) & 0xff,
        (value >> 16) & 0xff,
        (value >> 24) & 0xff,
    };
    return bzm_serial_write_register_to(
        context, asic_id, engine_id, offset, bytes, sizeof(bytes));
}

static bool driver_live_read_u32(void *context, uint8_t asic_id,
                                 uint16_t engine_id, uint8_t offset,
                                 uint32_t *value)
{
    if (!driver_live_mining_check() || value == NULL) return false;
    uint8_t bytes[4];
    if (!bzm_serial_read_register(
            context, asic_id, engine_id, offset, bytes, sizeof(bytes))) {
        return false;
    }
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

static void driver_live_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    while (delay_ms != 0) {
        if (!driver_live_mining_check()) return;
        const uint32_t chunk =
            delay_ms > BZM_LEASE_GUARD_MAX_DELAY_CHUNK_MS
                ? BZM_LEASE_GUARD_MAX_DELAY_CHUNK_MS
                : delay_ms;
        driver_sleep(NULL, chunk);
        delay_ms -= chunk;
    }
}

static bool driver_operation_check(void)
{
    if (!DRIVER_LEASE_IO_OK || DRIVER_OPERATION_AUTHORIZE == NULL ||
        !DRIVER_OPERATION_AUTHORIZE(DRIVER_OPERATION_AUTHORIZE_CONTEXT)) {
        DRIVER_LEASE_IO_OK = false;
        return false;
    }
    return true;
}

static void driver_sleep(void * context, uint32_t delay_ms)
{
    (void) context;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static bzm_bringup_probe_result_t driver_probe_noop(void * context, uint8_t asic_id)
{
    if (!driver_lease_check()) {
        return BZM_BRINGUP_PROBE_IO_ERROR;
    }
    bzm_serial_probe_result_t result = bzm_serial_probe_noop(context, asic_id);
    switch (result) {
    case BZM_SERIAL_PROBE_RESPONSE:
        return BZM_BRINGUP_PROBE_RESPONSE;
    case BZM_SERIAL_PROBE_NO_RESPONSE:
        return BZM_BRINGUP_PROBE_NO_RESPONSE;
    default:
        return BZM_BRINGUP_PROBE_IO_ERROR;
    }
}

static bool driver_write_u32(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, uint32_t value)
{
    if (!driver_lease_check()) {
        return false;
    }
    bzm_serial_transport_t * transport = context;
    uint8_t bytes[4] = {
        value & 0xff,
        (value >> 8) & 0xff,
        (value >> 16) & 0xff,
        (value >> 24) & 0xff,
    };
    return bzm_serial_write_register_to(transport, asic_id, engine_id, offset, bytes, sizeof(bytes));
}

static bool driver_read_u32(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, uint32_t * value)
{
    if (!driver_lease_check()) {
        return false;
    }
    bzm_serial_transport_t * transport = context;
    uint8_t bytes[4];
    if (value == NULL || !bzm_serial_read_register(transport, asic_id, engine_id, offset, bytes, sizeof(bytes))) {
        return false;
    }
    *value = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
    return true;
}

static void driver_delay_ms(void * context, uint32_t delay_ms)
{
    (void) context;
    if (!DRIVER_LEASE_IO_OK || !bzm_lease_guard_delay(delay_ms, driver_lease_renew, driver_sleep, NULL)) {
        DRIVER_LEASE_IO_OK = false;
    }
}

static uint64_t driver_now_us(void * context)
{
    (void) context;
    return (uint64_t) esp_timer_get_time();
}

static bool driver_telemetry_snapshot(void * context, bzm_telemetry_store_t * snapshot, uint16_t timeout_ms)
{
    if (!driver_lease_check()) {
        return false;
    }
    bzm_serial_transport_t * transport = context;
    /* Pump at least one receive window so stale pre-configuration evidence
     * cannot pass merely because it was already present in the store. */
    (void) bzm_serial_poll(transport, timeout_ms);
    return bzm_serial_get_telemetry_snapshot(transport, snapshot);
}

static bool driver_ramp_begin_engine(void * context, uint8_t asic_id, uint16_t engine_id)
{
    (void) context;
    (void) asic_id;
    (void) engine_id;
    return driver_operation_check();
}

static bool driver_ramp_write_control_u32(void * context, uint8_t asic_id, uint8_t offset, uint32_t value)
{
    uint8_t bytes[4] = {
        value & 0xff,
        (value >> 8) & 0xff,
        (value >> 16) & 0xff,
        (value >> 24) & 0xff,
    };
    return driver_operation_check() &&
           bzm_serial_write_register_to(context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, offset, bytes, sizeof(bytes));
}

static bool driver_ramp_read_control_u32(void * context, uint8_t asic_id, uint8_t offset, uint32_t * value)
{
    uint8_t bytes[4];
    if (value == NULL || !driver_operation_check() ||
        !bzm_serial_read_register(context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, offset, bytes, sizeof(bytes))) {
        return false;
    }
    *value = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) | ((uint32_t) bytes[2] << 16) |
             ((uint32_t) bytes[3] << 24);
    return true;
}

static bool driver_ramp_wait_tdm_idle(bzm_serial_transport_t * transport)
{
    /* ASICs occupy slots 10, 20, 30, and 40 in a 100-slot TDM cycle. Finish
     * the current response and require a one-millisecond byte-free interval
     * with no partial parser frame before changing TDM state. */
    for (uint8_t attempt = 0; attempt < 64; ++attempt) {
        size_t frames = bzm_serial_poll(transport, 1);
        bzm_serial_parser_stats_t stats;
        if (!bzm_serial_get_parser_stats(transport, &stats)) {
            return false;
        }
        if (frames == 0 && stats.buffered_bytes == 0) {
            return true;
        }
    }
    bzm_serial_parser_stats_t stats = {0};
    (void) bzm_serial_get_parser_stats(transport, &stats);
    ESP_LOGE(TAG, "Engine startup could not reach a TDM idle gap: frames=%lu discarded=%lu buffered=%u",
             (unsigned long) stats.emitted_frames, (unsigned long) stats.discarded_bytes, (unsigned) stats.buffered_bytes);
    return false;
}

static bool driver_ramp_capture_batch_telemetry(bzm_serial_transport_t * transport)
{
    bzm_telemetry_confirmation_t confirmation;
    bzm_telemetry_confirmation_init(&confirmation);
    uint8_t attempts = DRIVER_RAMP_TELEMETRY_POLICY.ch2_confirm_samples * BZM_BRINGUP_ASIC_COUNT;
    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt != 0) {
            (void) bzm_serial_poll(transport, 30);
        }
        if (!driver_ramp_wait_tdm_idle(transport)) {
            return false;
        }

        bzm_telemetry_store_t snapshot;
        if (!bzm_serial_get_telemetry_snapshot(transport, &snapshot)) {
            return false;
        }
        uint64_t now_us = (uint64_t) esp_timer_get_time();
        bool hard_fault = false;
        for (size_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
            uint8_t asic_id = bzm_asic_wire_ids[index];
            const bzm_telemetry_sample_t * sample = bzm_telemetry_store_get(&snapshot, asic_id);
            if (bzm_telemetry_sample_has_immediate_trip(sample)) {
                hard_fault = true;
            }
        }
        uint8_t culprit = 0;
        uint8_t observed = 0;
        bzm_ch2_confirmation_result_t confirmation_result = bzm_telemetry_confirmation_observe(
            &confirmation, &snapshot, now_us, DRIVER_RAMP_TELEMETRY_POLICY.max_age_us,
            &DRIVER_RAMP_TELEMETRY_POLICY.bounds, true, DRIVER_RAMP_TELEMETRY_POLICY.ch2_confirm_samples,
            &culprit, &observed);
        if (confirmation_result == BZM_CH2_CONFIRMATION_GOOD) {
            DRIVER_BATCH_TELEMETRY = snapshot;

            return true;
        }

        const bzm_telemetry_sample_t * culprit_sample = bzm_telemetry_store_get(&snapshot, culprit);

        if (hard_fault || confirmation_result == BZM_CH2_CONFIRMATION_CONTINUOUS ||
            confirmation_result == BZM_CH2_CONFIRMATION_INVALID) {
            ESP_LOGE(TAG,
                     "Engine startup telemetry unsafe: asic=0x%02x samples=%u/%u temp=%.1fC ch0=%.1fmV ch1=%.1fmV ch2=%.1fmV",
                     culprit, observed,
                     DRIVER_RAMP_TELEMETRY_POLICY.ch2_confirm_samples,
                     culprit_sample != NULL ? culprit_sample->temperature_c : 0.0f,
                     culprit_sample != NULL ? culprit_sample->ch0_mv : 0.0f,
                     culprit_sample != NULL ? culprit_sample->ch1_mv : 0.0f,
                     culprit_sample != NULL ? culprit_sample->ch2_mv : 0.0f);
            return false;
        }
    }
    return false;
}

static bool driver_ramp_set_tdm(void * context, bool enabled)
{
    bzm_serial_transport_t * transport = context;
    uint32_t control = bzm_bringup_reference_tdm_control();
    if (!enabled) {
        control &= ~1U;
    }
    if (!driver_lease_check()) {
        return false;
    }
    if (!enabled && !driver_ramp_capture_batch_telemetry(transport)) {
        return false;
    }
    /* One broadcast reaches every ASIC inside the same TDM idle window. Four
     * sequential unacknowledged writes can otherwise leave the chain in a
     * mixed transport mode if the later write overlaps the next slot burst. */
    if (!driver_ramp_write_control_u32(transport, BZM_ALL_ASICS, BZM_LOCAL_REG_UART_TDM_CONTROL, control)) {
        return false;
    }
    if (BZM_SERIAL_wait_tx_done(100) != ESP_OK) {
        return false;
    }
    if (!enabled) {
        for (size_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
            uint8_t asic_id = bzm_asic_wire_ids[index];
            uint32_t actual = 0;
            bool read_ok = false;
            for (uint8_t attempt = 0; attempt < 3 && !read_ok; ++attempt) {
                read_ok = driver_ramp_read_control_u32(transport, asic_id, BZM_LOCAL_REG_UART_TDM_CONTROL, &actual);
                if (!read_ok && attempt < 2) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
            if (!read_ok || actual != control) {
                ESP_LOGE(TAG,
                         "Engine startup TDM pause readback failed: asic=0x%02x expected=0x%08lx actual=0x%08lx read_ok=%u",
                         asic_id, (unsigned long) control, (unsigned long) actual, read_ok);
                return false;
            }
        }
    }
    return driver_operation_check();
}

static bool driver_balanced_batch_begin(void *context, uint16_t pair_index)
{
    (void)pair_index;
    return driver_ramp_set_tdm(context, false);
}

static bool driver_balanced_batch_end(void *context, uint16_t pair_index)
{
    (void)pair_index;
    bool resumed = driver_ramp_set_tdm(context, true);

    return resumed;
}

static bool driver_ramp_write_register(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset,
                                       const void * data, size_t data_len)
{
    return driver_operation_check() &&
           bzm_serial_write_register_to(context, asic_id, engine_id, offset, data, data_len);
}

static bool driver_ramp_read_register(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data,
                                      size_t data_len)
{
    if (!driver_operation_check()) {
        return false;
    }
    return bzm_serial_read_register(context, asic_id, engine_id, offset,
                                    data, data_len);
}

static void driver_ramp_delay_ms(void * context, uint32_t delay_ms)
{
    (void) context;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static bool driver_ramp_telemetry_sample(void *context, uint8_t asic_id, bzm_telemetry_sample_t *sample)
{
    (void)context;
    if (!driver_operation_check() || sample == NULL) return false;
    /* The batch-begin hook captures all four ASICs before pausing TDM. */
    const bzm_telemetry_sample_t *cached = bzm_telemetry_store_get(&DRIVER_BATCH_TELEMETRY, asic_id);
    if (cached == NULL) return false;
    *sample = *cached;
    return true;
}

static bool driver_ramp_final_barrier(void * context)
{
    bzm_serial_transport_t * transport = context;
    if (!driver_lease_check() || BZM_SERIAL_wait_tx_done(1000) != ESP_OK) {
        return false;
    }
    (void) bzm_serial_poll(transport, 30);
    return driver_lease_check();
}

static const bzm_balanced_ramp_ops_t DRIVER_BALANCED_RAMP_OPS = {
    .begin_engine = driver_ramp_begin_engine,
    .write_register = driver_ramp_write_register,
    .read_register = driver_ramp_read_register,
    .delay_ms = driver_ramp_delay_ms,
    .telemetry_sample = driver_ramp_telemetry_sample,
};

static bool driver_balanced_pair_commit(void * context, uint8_t asic_id, const bzm_engine_pair_t * pair)
{
    bool committed = bzm_balanced_ramp_commit_pair(&DRIVER_BALANCED_RAMP, &DRIVER_BALANCED_RAMP_OPS, context, asic_id, pair);
    if (!committed) {
        ESP_LOGE(TAG,
                 "Engine pair activation failed: cause=%s asic=0x%02x engine=0x%03x reg=0x%02x expected=%lu actual=%lu",
                 bzm_balanced_ramp_failure_name(DRIVER_BALANCED_RAMP.failure), DRIVER_BALANCED_RAMP.failure_asic_id,
                 DRIVER_BALANCED_RAMP.failure_engine_id, DRIVER_BALANCED_RAMP.failure_register_offset,
                 (unsigned long) DRIVER_BALANCED_RAMP.failure_expected, (unsigned long) DRIVER_BALANCED_RAMP.failure_actual);
    }
    return committed;
}

static const bzm_bringup_ops_t DRIVER_OPS = {
    .probe_noop = driver_probe_noop,
    .write_u32 = driver_write_u32,
    .read_u32 = driver_read_u32,
    .delay_ms = driver_delay_ms,
    .now_us = driver_now_us,
    .telemetry_snapshot = driver_telemetry_snapshot,

    .balanced_batch_begin = driver_balanced_batch_begin,
    .balanced_pair_commit = driver_balanced_pair_commit,
    .balanced_batch_end = driver_balanced_batch_end,
    .activation_barrier = driver_ramp_final_barrier,
};

static const bzm_bringup_ops_t DRIVER_LIVE_FREQUENCY_OPS = {
    .write_u32 = driver_live_write_u32,
    .read_u32 = driver_live_read_u32,
    .delay_ms = driver_live_delay_ms,
    .now_us = driver_now_us,
};

static bool driver_mining_write_work(void * context, const bzm_work_t * work)
{
    /* Called once per engine from the reactor while REACTOR_LOCK is held.
     * Re-checking the external lease/interlock callback here stops a long
     * multi-engine dispatch as soon as authorization is withdrawn. */
    if (!DRIVER_TRANSPORT_READY || !DRIVER_BRINGUP.running || !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE)) {
        return false;
    }
    return bzm_serial_write_work(context, work);
}

static bool driver_mining_dispatch_checkpoint(void * context)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL || !DRIVER_TRANSPORT_READY ||
        !DRIVER_BRINGUP.running ||
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE) ||
        !driver_mining_lease_service_due() ||
        BZM_SERIAL_wait_tx_done(100) != ESP_OK) {
        return false;
    }

    /* A full 236-engine dispatch keeps the 2 Mbaud ESP-to-bridge link busy
     * for a bounded interval. The bridge is also
     * forwarding addressed TDM telemetry/results in the other direction.
     * Give that receive path a bounded idle interval, then drain the ESP RX
     * ring before programming the next logical engine. The frame parser resynchronizes
     * malformed traffic; the board monitor checks telemetry freshness. */
    esp_rom_delay_us(BZM_DISPATCH_GAP_US);
    (void) bzm_serial_poll(transport, 1);
    return bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE);
}

static const bzm_transport_ops_t DRIVER_MINING_OPS = {
    .write_work = driver_mining_write_work,
    .dispatch_checkpoint = driver_mining_dispatch_checkpoint,
    .flush = bzm_serial_flush,
};

static bool driver_fail_closed_locked(void)
{
    INITIALIZED = false;
    DRIVER_TRANSPORT_READY = false;
    DRIVER_LEASE_IO_OK = false;
    DRIVER_LEASE_SCHEDULE = (bzm_lease_guard_schedule_t){0};
    TRANSPORT.asic_count = 0;
    bzm_bringup_init(&DRIVER_BRINGUP);
    DRIVER_BALANCED_RAMP = (bzm_balanced_ramp_report_t){0};

    bzm_dispatch_gate_init(&DRIVER_DISPATCH_GATE);
    return BZM_bridge_set_asic_reset(false) == ESP_OK;
}

bzm_bringup_outcome_t BZM_start(GlobalState *state, const bzm_bringup_telemetry_policy_t *policy,
                                 bzm_bringup_report_t *report)
{
    if (!BZM_driver_state_init(state)) {
        driver_report(report, BZM_BRINGUP_REASON_INVALID_ARGUMENT);
        return BZM_BRINGUP_BAD;
    }
    pthread_mutex_lock(&REACTOR_LOCK);
    if (policy == NULL || state->DEVICE_CONFIG.family.asic.core_count != BZM_ENGINES_PER_ASIC ||
        state->DEVICE_CONFIG.family.asic_count != BZM_BRINGUP_ASIC_COUNT) {
        driver_report(report, BZM_BRINGUP_REASON_INVALID_ARGUMENT);
        goto failed;
    }

    INITIALIZED = false;
    DRIVER_TRANSPORT_READY = false;
    DRIVER_LEASE_IO_OK = true;
    atomic_store_explicit(&BZM_STATE->io_failed, false, memory_order_release);
    DRIVER_LEASE_SCHEDULE = (bzm_lease_guard_schedule_t){0};
    bzm_bringup_init(&DRIVER_BRINGUP);
    DRIVER_BALANCED_RAMP = (bzm_balanced_ramp_report_t){0};
    DRIVER_RAMP_TELEMETRY_POLICY = *policy;

    bzm_dispatch_gate_init(&DRIVER_DISPATCH_GATE);

    bool lease_ready = driver_lease_check();
    bool reset_held = lease_ready && BZM_bridge_set_asic_reset(false) == ESP_OK;
    bool serial_ready = BZM_SERIAL_prepare_session(BZM_BAUD_RATE) == ESP_OK;
    bzm_serial_transport_deinit(&TRANSPORT);
    TRANSPORT = (bzm_serial_transport_t){
        .engine_count = BZM_ENGINES_PER_ASIC,
    };
    bool transport_ready = bzm_serial_transport_init(&TRANSPORT);
    DRIVER_TRANSPORT_READY = lease_ready && reset_held && serial_ready && transport_ready;
    if (!DRIVER_TRANSPORT_READY) {
        driver_report(report, BZM_BRINGUP_REASON_IO);
        goto failed;
    }

    /* Renew immediately before the bounded 200 ms reset pulse. */
    if (!driver_lease_check() || BZM_bridge_pulse_asic_reset() != ESP_OK) {
        driver_report(report, BZM_BRINGUP_REASON_IO);
        goto failed;
    }
    driver_delay_ms(&TRANSPORT, 1000);
    if (bzm_bringup_start(&DRIVER_BRINGUP, &DRIVER_OPS, &TRANSPORT, policy, report) != BZM_BRINGUP_GOOD) {
        if (report != NULL && DRIVER_BALANCED_RAMP.failure != BZM_BALANCED_RAMP_FAILURE_NONE) {
            report->asic_id = DRIVER_BALANCED_RAMP.failure_asic_id;
            report->register_offset = DRIVER_BALANCED_RAMP.failure_register_offset;
            report->expected = DRIVER_BALANCED_RAMP.failure_expected;
            report->actual = DRIVER_BALANCED_RAMP.failure_actual;
        }
        goto failed;
    }

    TRANSPORT.asic_count = BZM_BRINGUP_ASIC_COUNT;
    memcpy(TRANSPORT.asic_ids, bzm_asic_wire_ids, sizeof(bzm_asic_wire_ids));

    bzm_reactor_config_t config = {
        .engine_count = BZM_ENGINES_PER_ASIC,
        .timestamp_count = 60,
        .lead_zeros = BZM_RESULT_LEAD_ZEROS,
        .nonce_offset = BZM_NONCE_GAP_1002,
    };
    INITIALIZED = bzm_reactor_init(&REACTOR, &BZM_STATE->job_store, &config, &DRIVER_MINING_OPS, &TRANSPORT);
    if (!INITIALIZED) {
        driver_report(report, BZM_BRINGUP_REASON_IO);
        goto failed;
    }
    /* Prime every engine with fresh work, including after resume. */
    start_work_replacement_locked(config.engine_count);
    pthread_mutex_unlock(&REACTOR_LOCK);
    return BZM_BRINGUP_GOOD;

failed:
    (void) driver_fail_closed_locked();
    pthread_mutex_unlock(&REACTOR_LOCK);
    return BZM_BRINGUP_BAD;
}

bzm_bringup_outcome_t BZM_step_frequency_domains(
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report,
    float *actual_mhz)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    if (!DRIVER_TRANSPORT_READY || !DRIVER_LEASE_IO_OK ||
        !DRIVER_BRINGUP.running ||
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE)) {
        if (actual_mhz != NULL) *actual_mhz = DRIVER_BRINGUP.clock_mhz;
        pthread_mutex_unlock(&REACTOR_LOCK);
        driver_report(report, BZM_BRINGUP_REASON_PREREQUISITE);
        return BZM_BRINGUP_BAD;
    }

    bzm_bringup_outcome_t outcome =
        bzm_bringup_live_frequency_domains_step(
            &DRIVER_BRINGUP, &DRIVER_LIVE_FREQUENCY_OPS, &TRANSPORT,
            target_mhz, allow_initial_jump, report);
    if (outcome == BZM_BRINGUP_GOOD) {
        /* Keep mining live; replace work that crossed the PLL transition. */
        bzm_reactor_invalidate_work(&REACTOR);
        bzm_serial_discard_pending_results(&TRANSPORT);
        // A PLL transition also preserves the logical pool-job identity.
        start_work_replacement_locked(REACTOR.config.engine_count);
    }
    if (actual_mhz != NULL) *actual_mhz = DRIVER_BRINGUP.clock_mhz;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return outcome;
}

bool BZM_get_state(bzm_bringup_state_t * state)
{
    if (state == NULL)
        return false;
    pthread_mutex_lock(&REACTOR_LOCK);
    *state = DRIVER_BRINGUP;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return true;
}

bool BZM_hold_reset(void)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    bool held = driver_fail_closed_locked();
    pthread_mutex_unlock(&REACTOR_LOCK);
    return held;
}

void BZM_set_dispatch_authorizer(bzm_dispatch_authorizer_t authorize, void * context)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    bzm_dispatch_gate_set(&DRIVER_DISPATCH_GATE, authorize, context);
    pthread_mutex_unlock(&REACTOR_LOCK);
}

void BZM_set_operation_authorizer(bzm_dispatch_authorizer_t authorize, void * context)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    DRIVER_OPERATION_AUTHORIZE = authorize;
    DRIVER_OPERATION_AUTHORIZE_CONTEXT = context;
    pthread_mutex_unlock(&REACTOR_LOCK);
}

size_t BZM_poll(uint16_t timeout_ms)
{
    if (timeout_ms == 0)
        return 0;
    pthread_mutex_lock(&REACTOR_LOCK);
    size_t frames = DRIVER_TRANSPORT_READY ? bzm_serial_poll(&TRANSPORT, timeout_ms) : 0;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return frames;
}

/* Translate only at the driver boundary. PR #1972 already carries an owned
 * common job: resolving its ntime here needs no extra shared result fields. */
task_result *BZM_process_work(GlobalState *state)
{
    const bzm_result_t *share = BZM_process_result(state);
    if (share == NULL) return NULL;
    static task_result result;
    if (!bzm_result_to_task(share, &result) ||
        !bzm_job_store_contains(&BZM_STATE->job_store, share->work_handle)) return NULL;
    double difficulty = mining_nonce_difficulty(&result.job, result.nonce,
                                                result.rolled_version);
    bool valid = isfinite(difficulty) && difficulty >= BZM_RESULT_DIFFICULTY;
    if (valid && share->asic_index < BZM_MAX_ASIC_COUNT) {
        atomic_fetch_add_explicit(&HASHRATE_DIFFICULTY_ONE_COUNTERS[share->asic_index],
                                  BZM_RESULT_DIFFICULTY, memory_order_relaxed);
    }
    return valid ? &result : NULL;
}

void BZM_submit_job(GlobalState *state, const asic_job_t *job)
{
    if (BZM_STATE == NULL || state == NULL || job == NULL) return;
    unsigned epoch = atomic_load_explicit(&BZM_STATE->work_epoch, memory_order_acquire);
    while (state->ASIC_initalized && !state->SYSTEM_MODULE.mining_paused &&
           !state->SYSTEM_MODULE.hardware_fault &&
           !state->SYSTEM_MODULE.pools_unavailable &&
           epoch == atomic_load_explicit(&BZM_STATE->work_epoch, memory_order_acquire)) {
        if (BZM_send_work(state, job)) return;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool BZM_has_io_fault(void)
{
    return BZM_STATE == NULL ||
        atomic_load_explicit(&BZM_STATE->io_failed, memory_order_acquire);
}
