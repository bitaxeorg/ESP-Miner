#include "bzm/platform.h"
#include "bzm/telemetry_decode.h"
#include "bzm/runtime.h"
#include "bzm/board.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "bzm/controls.h"
#include "bzm/frequency.h"
#include <stdio.h>
#include "bzm/balanced_ramp.h"
#include "bzm/bringup.h"
#include "bzm/dispatch_gate.h"
#include "bzm/reactor.h"
#include "bzm/runtime_result.h"
#include "bzm/result_dedup.h"
#include "bzm/registers.h"
#include "bzm/transport.h"
#include "bzm/work_registers.h"
#include "bzm/hash.h"
#include "bzm/hashrate.h"
#include "bzm/platform_serial.h"

static const char * TAG = "bzm";

#define BZM_IDLE_CHECK_INTERVAL_MS 100.0

/* Bootstrap at D16 until the application controller selects its filter. */
#define BZM_INITIAL_LEAD_ZEROS 36U
#define BZM_CANCEL_CHECK_INTERVAL_MS 250U

typedef struct {
    bzm_job_store_t job_store;
    atomic_uint work_epoch;
    bzm_reactor_t reactor;
    bzm_serial_transport_t transport;
    pthread_mutex_t reactor_lock;
    bool initialized;
    atomic_bool io_failed;
    bool driver_transport_ready;
    bool driver_io_ok;
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
    pthread_mutex_t hashrate_lock;
    bzm_hashrate_t hashrate[BZM_MAX_ASIC_COUNT];
    atomic_uint_fast32_t work_replacement_generation;
    uint32_t work_replacement_completed; // Protected by REACTOR_LOCK.
    uint16_t work_replacement_remaining;
    uint16_t refill_remaining;
    int64_t refresh_at_us;
    void (*request_work)(void *context);
    void *request_work_context;
    bzm_result_dedup_t result_dedup;
} bzm_driver_state_t;

static bzm_driver_state_t *BZM_STATE;

#define REACTOR (BZM_STATE->reactor)
#define TRANSPORT (BZM_STATE->transport)
#define REACTOR_LOCK (BZM_STATE->reactor_lock)
#define INITIALIZED (BZM_STATE->initialized)
#define DRIVER_TRANSPORT_READY (BZM_STATE->driver_transport_ready)
#define DRIVER_IO_OK (BZM_STATE->driver_io_ok)
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
#define REFILL_REMAINING (BZM_STATE->refill_remaining)
#define RESULT_DEDUP (BZM_STATE->result_dedup)

static void driver_sleep(void *context, uint32_t delay_ms);

bool bzm_driver_state_init(bzm_device_t *state)
{
    if (state == NULL) {
        return false;
    }
    if (BZM_STATE != NULL) return true;

    bzm_driver_state_t *allocated = bzm_calloc(1, sizeof(*allocated), true);
    if (allocated == NULL) {
        BZM_LOGE(TAG, "Unable to allocate Bonanza driver state");
        return false;
    }
    if (pthread_mutex_init(&allocated->reactor_lock, NULL) != 0) {
        bzm_free(allocated);
        return false;
    }
    allocated->last_temperature = -1.0f;
    if (pthread_mutex_init(&allocated->hashrate_lock, NULL) != 0) {
        pthread_mutex_destroy(&allocated->reactor_lock);
        bzm_free(allocated);
        return false;
    }
    allocated->request_work = state->request_work;
    allocated->request_work_context = state->context;
    atomic_init(&allocated->work_epoch, 0);
    atomic_init(&allocated->io_failed, false);
    atomic_init(&allocated->work_replacement_generation, 0);
    for (size_t asic = 0; asic < BZM_MAX_ASIC_COUNT; ++asic) {
        atomic_init(&allocated->hashrate_difficulty_one_counters[asic], 0);
    }

    if (!bzm_job_store_init_with_memory(&allocated->job_store, true)) {
        pthread_mutex_destroy(&allocated->hashrate_lock);
        pthread_mutex_destroy(&allocated->reactor_lock);
        bzm_free(allocated);
        return false;
    }
    BZM_STATE = allocated;
    BZM_LOGI(TAG, "Allocated %u bytes of Bonanza driver state",
             (unsigned)sizeof(*allocated));
    return true;
}

static void reset_result_dedup(void)
{
    memset(&RESULT_DEDUP, 0, sizeof(RESULT_DEDUP));
}

static void start_work_replacement_locked(uint16_t remaining)
{
    REFILL_REMAINING = remaining;
    BZM_STATE->refresh_at_us = 0;
    BZM_STATE->work_replacement_remaining = remaining;
    if (remaining != 0) {
        atomic_fetch_add_explicit(
            &WORK_REPLACEMENT_GENERATION, 1, memory_order_relaxed);
        if (BZM_STATE->request_work)
            BZM_STATE->request_work(BZM_STATE->request_work_context);
    }
}

static int64_t work_refresh_interval_us(void)
{
    float fastest_mhz = DRIVER_BRINGUP.clock_mhz;
    for (size_t chip = 0; chip < BZM_BRINGUP_ASIC_COUNT; ++chip)
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll)
            if (DRIVER_BRINGUP.domain_clock_mhz[chip][pll] > fastest_mhz)
                fastest_mhz = DRIVER_BRINGUP.domain_clock_mhz[chip][pll];
    if (!(fastest_mhz > 0)) fastest_mhz = BZM_FREQUENCY_POWER_ON_MHZ;
    /* Renew at 80% of the shortest row's search time, using the fastest PLL.
     * Edge rows have fewer engines and therefore a longer nonce lifetime. */
    return (int64_t)(0.8 * 3.0 * (REACTOR.config.timestamp_count + 1.0) *
                     (UINT32_MAX + 1.0) / fastest_mhz / BZM_BRINGUP_ASIC_COUNT /
                     (REACTOR.config.multicast_rows ? BZM_ENGINE_COLUMNS : 1));
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

int bzm_driver_set_max_baud(void)
{
    return bzm_port_serial_set_baud(BZM_BAUD_RATE) == BZM_OK ? BZM_BAUD_RATE : 0;
}

static bool driver_send_work(bzm_device_t * state, const asic_job_t * template)
{
    (void) state;
    if (BZM_STATE == NULL || !INITIALIZED || template == NULL || bzm_driver_has_io_fault()) {
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
    int64_t dispatch_started_us = bzm_now_us();
    bool results_were_quarantined =
        bzm_reactor_results_quarantined(&REACTOR);

    bzm_assign_status_t status =
        bzm_reactor_assign(&REACTOR, template, NULL);
    if (status == BZM_ASSIGN_FLUSH_REQUIRED) {
        if (!bzm_reactor_begin_flush(&REACTOR)) {
            pthread_mutex_unlock(&REACTOR_LOCK);
            atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
            BZM_LOGE(TAG, "Unable to complete BZM flush barrier");
            return false;
        }
        bzm_reactor_finish_flush(&REACTOR);
        // Hardware sequence reuse does not make an already seen share new.
        start_work_replacement_locked(REACTOR.config.engine_count);
        results_were_quarantined = true;
        status = bzm_reactor_assign(&REACTOR, template, NULL);
    }
    bool refill_complete = status == BZM_ASSIGN_OK && REFILL_REMAINING == 1;
    if (status == BZM_ASSIGN_OK && BZM_STATE->work_replacement_remaining != 0 &&
        --BZM_STATE->work_replacement_remaining == 0)
        BZM_STATE->work_replacement_completed = (uint32_t)atomic_load_explicit(
            &WORK_REPLACEMENT_GENERATION, memory_order_relaxed);
    if (status == BZM_ASSIGN_OK && REFILL_REMAINING != 0)
        --REFILL_REMAINING;
    /* Begin staggered renewal immediately after a full refill. Waiting a whole
     * row lifetime first would let later rows run out before their next turn. */
    if (status == BZM_ASSIGN_OK && REFILL_REMAINING == 0)
        BZM_STATE->refresh_at_us = dispatch_started_us +
            work_refresh_interval_us() / REACTOR.config.engine_count;
    if (status == BZM_ASSIGN_OK && results_were_quarantined &&
        !bzm_reactor_results_quarantined(&REACTOR)) {
        /* A clean Stratum job makes every pre-flush result unsubmitable.
         * Drain anything queued during the bounded replacement rotation
         * before normal attribution resumes. */
        bzm_serial_discard_pending_results(&TRANSPORT);
        BZM_LOGI(TAG, "BZM sequence reuse barrier complete");
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
            BZM_LOGI(TAG,
                     "BZM work assignment cancelled after dispatch authorization was withdrawn");
            return false;
        }
        atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
        BZM_LOGE(TAG, "BZM work assignment failed (%d)", status);
        return false;
    }
    if (refill_complete) {
        BZM_LOGI(TAG,
                 "BZM multicast row refill complete");
    }
    return true;
}

bool bzm_driver_clear_work(bzm_device_t * state)
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
        if (INITIALIZED) REFILL_REMAINING = REACTOR.config.engine_count;
        BZM_STATE->refresh_at_us = 0;
    }
    pthread_mutex_unlock(&REACTOR_LOCK);
    if (!cleared) {
        atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
        BZM_LOGE(TAG, "BZM clean-job barrier failed");
    }
    return cleared;
}

bool bzm_driver_set_difficulty(bzm_device_t *state, uint8_t difficulty_power)
{
    if (BZM_STATE == NULL || state == NULL || difficulty_power < 4 || difficulty_power > 31)
        return false;
    pthread_mutex_lock(&REACTOR_LOCK);
    bool ready = INITIALIZED && !bzm_driver_has_io_fault();
    if (ready) {
        uint8_t lead_zeros = 32U + difficulty_power;
        if (REACTOR.config.lead_zeros != lead_zeros) {
            REACTOR.config.lead_zeros = lead_zeros;
            // Retain previous filters while refilling rows for the new difficulty.
            start_work_replacement_locked(REACTOR.config.engine_count);
        }
        state->difficulty_power = difficulty_power;
    }
    pthread_mutex_unlock(&REACTOR_LOCK);
    return ready;
}

double bzm_driver_job_frequency_ms(bzm_device_t *state)
{
    if (BZM_STATE == NULL || (state && (!state->initialized ||
        (state->can_submit && !state->can_submit(state->context)))))
        return BZM_IDLE_CHECK_INTERVAL_MS;
    pthread_mutex_lock(&REACTOR_LOCK);
    double interval_ms = BZM_IDLE_CHECK_INTERVAL_MS;
    if (INITIALIZED && !bzm_driver_has_io_fault() &&
        (!DRIVER_TRANSPORT_READY || DRIVER_BRINGUP.running)) {
        int64_t remaining_us = BZM_STATE->refresh_at_us - bzm_now_us();
        interval_ms = REFILL_REMAINING != 0 || remaining_us <= 0 ? 0 :
                      (double)((remaining_us + 999) / 1000);
    }
    pthread_mutex_unlock(&REACTOR_LOCK);
    return interval_ms;
}

static bzm_result_t * driver_receive_result(bzm_device_t * state)
{
    (void) state;
    if (BZM_STATE == NULL || !INITIALIZED ||
        (DRIVER_TRANSPORT_READY && !DRIVER_BRINGUP.running)) {
        // Let the lower-priority power task finish clearing the shared initialized state.
        bzm_sleep_ms(1);
        return NULL;
    }

    bzm_raw_result_t raw;
    static bzm_result_t event;
    pthread_mutex_lock(&REACTOR_LOCK);
    bool received = bzm_serial_read_result(&TRANSPORT, &raw, 20);
    if (received && bzm_reactor_results_quarantined(&REACTOR)) {
        pthread_mutex_unlock(&REACTOR_LOCK);
        bzm_sleep_ms(1);
        return NULL;
    }
    bool nonce_frame = received && bzm_raw_result_has_valid_nonce(&raw);
    bool mapped = nonce_frame && bzm_reactor_map_result(&REACTOR, &raw, &event);
    bool duplicate = mapped && result_is_duplicate(&event);
    pthread_mutex_unlock(&REACTOR_LOCK);
    if (duplicate) {
        bzm_sleep_ms(1);
        return NULL;
    }
    /* The lower-priority Stratum task must be able to acquire REACTOR_LOCK
     * to retire a disconnected pool. Repeated timed reads followed by an
     * immediate lock reacquisition otherwise starve that transition when
     * all arriving results are stale. Block outside the lock for one tick;
     * taskYIELD alone cannot schedule a lower-priority waiter. */
    if (!mapped)
        bzm_sleep_ms(1);
    return mapped ? &event : NULL;
}

bool bzm_driver_hashrate_counter_snapshot(bzm_device_t *state,
                                   uint32_t *difficulty_one_counters,
                                   size_t counter_count)
{
    if (BZM_STATE == NULL || state == NULL || difficulty_one_counters == NULL ||
        counter_count < state->asic_count ||
        state->asic_count > BZM_MAX_ASIC_COUNT) {
        return false;
    }
    for (size_t i = 0; i < state->asic_count; ++i) {
        difficulty_one_counters[i] = (uint32_t)atomic_load_explicit(
            &HASHRATE_DIFFICULTY_ONE_COUNTERS[i], memory_order_relaxed);
    }
    return true;
}

static bool hashrate_active(const bzm_device_t *state)
{
    return state->initialized && !bzm_driver_has_io_fault() &&
           (!state->can_submit || state->can_submit(state->context));
}

bool bzm_driver_get_hashrate_measurement(bzm_device_t *state, uint8_t asic_nr,
                                         uint64_t *time_us, float *hashrate)
{
    if (!state || !time_us || !hashrate || !BZM_STATE ||
        state->asic_count > BZM_MAX_ASIC_COUNT || asic_nr >= state->asic_count) return false;
    pthread_mutex_lock(&BZM_STATE->hashrate_lock);
    *time_us = BZM_STATE->hashrate[asic_nr].time_us;
    *hashrate = hashrate_active(state) ? BZM_STATE->hashrate[asic_nr].rate : 0;
    pthread_mutex_unlock(&BZM_STATE->hashrate_lock);
    return true;
}

bool bzm_driver_work_replacement_snapshot(uint32_t *generation, uint32_t *completed, bool *pending)
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

float bzm_driver_read_temperature(bzm_device_t * state)
{
    (void) state;
    if (BZM_STATE == NULL || !INITIALIZED || TRANSPORT.asic_count == 0)
        return -1.0f;

    int64_t now = bzm_now_us();
    if (LAST_TEMPERATURE_US != 0 && now - LAST_TEMPERATURE_US < 2000000) {
        return LAST_TEMPERATURE;
    }

    bzm_telemetry_store_t snapshot;
    pthread_mutex_lock(&REACTOR_LOCK);
    const bool available =
        bzm_serial_get_telemetry_snapshot(&TRANSPORT, &snapshot);
    pthread_mutex_unlock(&REACTOR_LOCK);

    now = bzm_now_us();
    float hottest_c = -1.0f;
    LAST_TEMPERATURE = -1.0f;
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

bool bzm_driver_get_telemetry_snapshot(bzm_telemetry_store_t * snapshot)
{
    return bzm_serial_get_telemetry_snapshot(&TRANSPORT, snapshot);
}

static bool driver_live_mining_check(void)
{
    if (!DRIVER_IO_OK || bzm_driver_has_io_fault() || !DRIVER_TRANSPORT_READY ||
        !DRIVER_BRINGUP.running ||
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE)) {
        DRIVER_IO_OK = false;
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
            delay_ms > BZM_CANCEL_CHECK_INTERVAL_MS
                ? BZM_CANCEL_CHECK_INTERVAL_MS
                : delay_ms;
        driver_sleep(NULL, chunk);
        delay_ms -= chunk;
    }
}

static bool driver_operation_check(void)
{
    if (!DRIVER_IO_OK || bzm_driver_has_io_fault() || DRIVER_OPERATION_AUTHORIZE == NULL ||
        !DRIVER_OPERATION_AUTHORIZE(DRIVER_OPERATION_AUTHORIZE_CONTEXT)) {
        DRIVER_IO_OK = false;
        return false;
    }
    return true;
}

static void driver_sleep(void * context, uint32_t delay_ms)
{
    (void) context;
    bzm_sleep_ms(delay_ms);
}

static bzm_bringup_probe_result_t driver_probe_noop(void * context, uint8_t asic_id)
{
    if (!driver_operation_check()) {
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
    if (!driver_operation_check()) {
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
    if (!driver_operation_check()) {
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
    while (delay_ms != 0) {
        if (!driver_operation_check()) return;
        uint32_t chunk = delay_ms > BZM_CANCEL_CHECK_INTERVAL_MS ? BZM_CANCEL_CHECK_INTERVAL_MS : delay_ms;
        driver_sleep(NULL, chunk);
        delay_ms -= chunk;
    }
    (void)driver_operation_check();
}

static uint64_t driver_now_us(void * context)
{
    (void) context;
    return (uint64_t) bzm_now_us();
}

static bool driver_telemetry_snapshot(void * context, bzm_telemetry_store_t * snapshot, uint16_t timeout_ms)
{
    if (!driver_operation_check()) {
        return false;
    }
    bzm_serial_transport_t * transport = context;
    int64_t started_us = bzm_now_us();
    int64_t deadline_us = started_us + (int64_t)timeout_ms * 1000;
    /* A poll may return after one byte or one ASIC's TDM slot. Keep reading
     * within this window so fragmented traffic cannot consume all of the
     * caller's acquisition attempts before the other ASICs report. */
    do {
        int64_t remaining_us = deadline_us - bzm_now_us();
        if (remaining_us <= 0) break;
        if (!driver_operation_check()) return false;
        if (bzm_serial_poll(transport, (uint16_t)((remaining_us + 999) / 1000)).status != BZM_OK)
            return false;
        if (!bzm_serial_get_telemetry_snapshot(transport, snapshot)) return false;
        bool all_updated = true;
        for (unsigned i = 0; i < BZM_BRINGUP_ASIC_COUNT; ++i) {
            const bzm_telemetry_sample_t *sample = bzm_telemetry_store_get(snapshot, bzm_asic_wire_ids[i]);
            if (sample == NULL || !sample->received || sample->timestamp_us < (uint64_t)started_us) {
                all_updated = false;
                break;
            }
        }
        if (all_updated) return true;
    } while (bzm_now_us() < deadline_us);
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
        bzm_serial_poll_result_t poll = bzm_serial_poll(transport, 1);
        if (poll.status != BZM_OK) return false;
        bzm_serial_parser_stats_t stats;
        if (!bzm_serial_get_parser_stats(transport, &stats)) {
            return false;
        }
        if (poll.bytes_received == 0 && stats.buffered_bytes == 0) {
            return true;
        }
    }
    bzm_serial_parser_stats_t stats = {0};
    (void) bzm_serial_get_parser_stats(transport, &stats);
    BZM_LOGE(TAG, "Engine startup could not reach a TDM idle gap: frames=%lu discarded=%lu buffered=%u",
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
            if (bzm_serial_poll(transport, 30).status != BZM_OK) return false;
        }
        if (!driver_ramp_wait_tdm_idle(transport)) {
            return false;
        }

        bzm_telemetry_store_t snapshot;
        if (!bzm_serial_get_telemetry_snapshot(transport, &snapshot)) {
            return false;
        }
        uint64_t now_us = (uint64_t) bzm_now_us();
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
            BZM_LOGE(TAG,
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
    if (!driver_operation_check()) {
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
    if (bzm_port_serial_wait_tx_done(100) != BZM_OK) {
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
                    bzm_sleep_ms(1);
                }
            }
            if (!read_ok || actual != control) {
                BZM_LOGE(TAG,
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
    bzm_sleep_ms(delay_ms);
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
    if (!driver_operation_check() || bzm_port_serial_wait_tx_done(1000) != BZM_OK) {
        return false;
    }
    return bzm_serial_poll(transport, 30).status == BZM_OK && driver_operation_check();
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
        BZM_LOGE(TAG,
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
     * Re-checking the external interlock callback here stops a long
     * multi-engine dispatch as soon as authorization is withdrawn. */
    if (!DRIVER_TRANSPORT_READY || bzm_driver_has_io_fault() ||
        !DRIVER_BRINGUP.running || !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE)) {
        return false;
    }
    return bzm_serial_write_work(context, work);
}

static bool driver_mining_dispatch_checkpoint(void * context)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL || !DRIVER_TRANSPORT_READY || bzm_driver_has_io_fault() ||
        !DRIVER_BRINGUP.running ||
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE)) {
        return false;
    }
    /* Queue assignments while generating the next job. A completed refill or
     * power-control replacement must include the last job's wire transmission. */
    if ((REFILL_REMAINING <= 1 || BZM_STATE->work_replacement_remaining == 1) &&
        bzm_port_serial_wait_tx_done(100) != BZM_OK) {
        return false;
    }

    /* Drain RX between logical-engine assignments without a dispatch delay. */
    return bzm_serial_poll(transport, 0).status == BZM_OK &&
           bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE);
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
    DRIVER_IO_OK = false;
    TRANSPORT.asic_count = 0;
    bzm_bringup_init(&DRIVER_BRINGUP);
    DRIVER_BALANCED_RAMP = (bzm_balanced_ramp_report_t){0};

    bzm_dispatch_gate_init(&DRIVER_DISPATCH_GATE);
    return bzm_control_set_asic_reset(false) == BZM_OK;
}

static void driver_transport_io_error(void *context)
{
    (void)context;
    atomic_store_explicit(&BZM_STATE->io_failed, true, memory_order_release);
}

static bool driver_partition_rows(void)
{
    /* Keep the long startup sentinel ranges throughout balanced activation.
     * Install row partitions only after the complete chain is running. */
    for (uint16_t index = 0; index < BZM_ENGINES_PER_ASIC; ++index) {
        bzm_engine_location_t engine;
        if (!bzm_topology_activation_at(index, BZM_ENGINE_STACK_BOTTOM, &engine)) return false;
        for (size_t chip = 0; chip < BZM_MAX_ASIC_COUNT; ++chip) {
            uint32_t start, end;
            uint8_t asic = bzm_asic_wire_ids[chip];
            if (!bzm_partition_row_nonce_range(asic, engine.physical_id, &start, &end) ||
                !driver_write_u32(&TRANSPORT, asic, engine.physical_id, BZM_REG_START_NONCE, start) ||
                !driver_write_u32(&TRANSPORT, asic, engine.physical_id, BZM_REG_END_NONCE, end) ||
                bzm_serial_poll(&TRANSPORT, 0).status != BZM_OK) return false;
        }
    }
    return bzm_port_serial_wait_tx_done(100) == BZM_OK && driver_operation_check();
}

bzm_bringup_outcome_t bzm_driver_start(bzm_device_t *state, const bzm_bringup_telemetry_policy_t *policy,
                                 bzm_bringup_report_t *report)
{
    if (!bzm_driver_state_init(state)) {
        driver_report(report, BZM_BRINGUP_REASON_INVALID_ARGUMENT);
        return BZM_BRINGUP_BAD;
    }
    pthread_mutex_lock(&REACTOR_LOCK);
    if (policy == NULL || state->core_count != BZM_ENGINES_PER_ASIC ||
        state->asic_count != BZM_BRINGUP_ASIC_COUNT) {
        driver_report(report, BZM_BRINGUP_REASON_INVALID_ARGUMENT);
        goto failed;
    }

    INITIALIZED = false;
    LAST_TEMPERATURE = -1.0f;
    LAST_TEMPERATURE_US = 0;
    DRIVER_TRANSPORT_READY = false;
    DRIVER_IO_OK = true;
    atomic_store_explicit(&BZM_STATE->io_failed, false, memory_order_release);
    bzm_bringup_init(&DRIVER_BRINGUP);
    DRIVER_BALANCED_RAMP = (bzm_balanced_ramp_report_t){0};
    DRIVER_RAMP_TELEMETRY_POLICY = *policy;

    bzm_dispatch_gate_init(&DRIVER_DISPATCH_GATE);

    bool authorized = driver_operation_check();
    bool reset_held = authorized && bzm_control_set_asic_reset(false) == BZM_OK;
    bool serial_ready = bzm_port_serial_prepare_session(BZM_BAUD_RATE) == BZM_OK;
    bzm_serial_transport_deinit(&TRANSPORT);
    TRANSPORT = (bzm_serial_transport_t){
        .engine_count = BZM_ENGINES_PER_ASIC,
    };
    bzm_serial_ops_t serial_ops = bzm_platform_serial_ops;
    serial_ops.on_io_error = driver_transport_io_error;
    bool transport_ready = bzm_serial_transport_init(&TRANSPORT, &serial_ops, NULL);
    DRIVER_TRANSPORT_READY = authorized && reset_held && serial_ready && transport_ready;
    if (!DRIVER_TRANSPORT_READY) {
        BZM_LOGE(TAG, "Startup I/O unavailable: authorized=%u reset=%u serial=%u transport=%u",
                 authorized, reset_held, serial_ready, transport_ready);
        driver_report(report, BZM_BRINGUP_REASON_IO);
        goto failed;
    }

    if (!driver_operation_check() || bzm_control_pulse_asic_reset() != BZM_OK) {
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
    if (!driver_partition_rows()) {
        driver_report(report, BZM_BRINGUP_REASON_IO);
        goto failed;
    }

    bzm_reactor_config_t config = {
        .engine_count = BZM_ENGINE_ROWS,
        .multicast_rows = true,
        .timestamp_count = 127, // Seven count bits encode 128 timestamp values.
        .lead_zeros = BZM_INITIAL_LEAD_ZEROS,
        .nonce_offset = BZM_NONCE_GAP,
    };
    INITIALIZED = bzm_reactor_init(&REACTOR, &BZM_STATE->job_store, &config, &DRIVER_MINING_OPS, &TRANSPORT);
    if (!INITIALIZED) {
        driver_report(report, BZM_BRINGUP_REASON_IO);
        goto failed;
    }
    /* Prime every engine with fresh work, including after resume. */
    start_work_replacement_locked(config.engine_count);
    state->difficulty_power = 0; // Let the controller apply its initial value immediately.
    pthread_mutex_unlock(&REACTOR_LOCK);
    return BZM_BRINGUP_GOOD;

failed:
    (void) driver_fail_closed_locked();
    pthread_mutex_unlock(&REACTOR_LOCK);
    return BZM_BRINGUP_BAD;
}

bzm_bringup_outcome_t bzm_driver_step_frequency_domains(
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report,
    float *actual_mhz)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    if (!DRIVER_TRANSPORT_READY || !DRIVER_IO_OK || bzm_driver_has_io_fault() ||
        !DRIVER_BRINGUP.running ||
        !bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE)) {
        if (actual_mhz != NULL) *actual_mhz = DRIVER_BRINGUP.clock_mhz;
        pthread_mutex_unlock(&REACTOR_LOCK);
        driver_report(report, BZM_BRINGUP_REASON_PREREQUISITE);
        return BZM_BRINGUP_BAD;
    }

    if (bzm_port_serial_wait_tx_done(100) != BZM_OK) {
        driver_transport_io_error(NULL);
        if (actual_mhz != NULL) *actual_mhz = DRIVER_BRINGUP.clock_mhz;
        pthread_mutex_unlock(&REACTOR_LOCK);
        driver_report(report, BZM_BRINGUP_REASON_IO);
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

bool bzm_driver_get_state(bzm_bringup_state_t * state)
{
    if (BZM_STATE == NULL || state == NULL)
        return false;
    pthread_mutex_lock(&REACTOR_LOCK);
    *state = DRIVER_BRINGUP;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return true;
}

bool bzm_driver_hold_reset(void)
{
    if (BZM_STATE == NULL) return bzm_control_set_asic_reset(false) == BZM_OK;
    pthread_mutex_lock(&REACTOR_LOCK);
    bool held = driver_fail_closed_locked();
    pthread_mutex_unlock(&REACTOR_LOCK);
    return held;
}

void bzm_driver_set_dispatch_authorizer(bzm_dispatch_authorizer_t authorize, void * context)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    bzm_dispatch_gate_set(&DRIVER_DISPATCH_GATE, authorize, context);
    pthread_mutex_unlock(&REACTOR_LOCK);
}

void bzm_driver_set_operation_authorizer(bzm_dispatch_authorizer_t authorize, void * context)
{
    pthread_mutex_lock(&REACTOR_LOCK);
    DRIVER_OPERATION_AUTHORIZE = authorize;
    DRIVER_OPERATION_AUTHORIZE_CONTEXT = context;
    pthread_mutex_unlock(&REACTOR_LOCK);
}

size_t bzm_driver_poll(uint16_t timeout_ms)
{
    if (timeout_ms == 0)
        return 0;
    pthread_mutex_lock(&REACTOR_LOCK);
    size_t frames = DRIVER_TRANSPORT_READY ? bzm_serial_poll(&TRANSPORT, timeout_ms).frames_emitted : 0;
    pthread_mutex_unlock(&REACTOR_LOCK);
    return frames;
}

const bzm_result_t *bzm_driver_process_result(bzm_device_t *state)
{
    const bzm_result_t *share = driver_receive_result(state);
    if (share == NULL || !share->job_valid ||
        !bzm_job_store_contains(&BZM_STATE->job_store, share->work_handle)) return NULL;
    asic_job_t job = share->job;
    job.ntime = share->final_ntime;
    uint8_t hash[32];
    if (!bzm_nonce_hash(&job, share->nonce, share->final_version, hash)) return NULL;
    /* A clean job may preempt software validation after the initial lookup. */
    if (!bzm_job_store_contains(&BZM_STATE->job_store, share->work_handle)) return NULL;
    if (share->difficulty_power < 4 || share->difficulty_power > 31) return NULL;
    bool valid = bzm_hash_meets_difficulty(hash, share->difficulty_power);
    if (valid && share->asic_index < BZM_MAX_ASIC_COUNT) {
        atomic_fetch_add_explicit(&HASHRATE_DIFFICULTY_ONE_COUNTERS[share->asic_index],
                                  UINT64_C(1) << share->difficulty_power, memory_order_relaxed);
    }
    return valid ? share : NULL;
}

void bzm_driver_submit_job(bzm_device_t *state, const asic_job_t *job)
{
    if (BZM_STATE == NULL || state == NULL || job == NULL) return;
    unsigned epoch = atomic_load_explicit(&BZM_STATE->work_epoch, memory_order_acquire);
    while (state->initialized && !bzm_driver_has_io_fault() &&
           (state->can_submit == NULL || state->can_submit(state->context)) &&
           epoch == atomic_load_explicit(&BZM_STATE->work_epoch, memory_order_acquire)) {
        if (driver_send_work(state, job)) return;
        bzm_sleep_ms(20);
    }
}

bool bzm_driver_has_io_fault(void)
{
    return BZM_STATE == NULL ||
        atomic_load_explicit(&BZM_STATE->io_failed, memory_order_acquire);
}

static bool device_io_allowed(void *context)
{
    (void)context;
    bzm_control_status_t status;
    return bzm_control_get_safety_status(&status) == BZM_OK && status.valid &&
           status.fault == BZM_CONTROL_FAULT_NONE && status.core_enabled &&
           status.five_volt_enabled;
}

static void device_fault(bzm_device_t *state, const char *reason)
{
    if (state->report_fault) state->report_fault(state->context, reason);
}

void bzm_driver_service(bzm_device_t *state)
{
    if (state == NULL || BZM_STATE == NULL) return;
    bzm_driver_poll(1);
    bzm_control_status_t status;
    if (bzm_driver_has_io_fault()) device_fault(state, "BZM transport failed");
    else if (bzm_control_get_safety_status(&status) == BZM_OK &&
             status.fault == BZM_CONTROL_FAULT_ASIC_TRIP)
        device_fault(state, "BZM ASIC trip");
    bool active = hashrate_active(state);
    uint64_t now_us = bzm_now_us();
    pthread_mutex_lock(&BZM_STATE->hashrate_lock);
    for (uint8_t asic = 0; asic < state->asic_count && asic < BZM_MAX_ASIC_COUNT; ++asic) {
        uint64_t counter = atomic_load_explicit(&HASHRATE_DIFFICULTY_ONE_COUNTERS[asic], memory_order_relaxed);
        bzm_hashrate_sample(&BZM_STATE->hashrate[asic], counter, now_us, active);
    }
    pthread_mutex_unlock(&BZM_STATE->hashrate_lock);
}

uint8_t bzm_driver_init(bzm_device_t *state)
{
    if (!bzm_driver_state_init(state)) return 0;
    bzm_driver_set_operation_authorizer(device_io_allowed, NULL);
    const bzm_bringup_telemetry_policy_t policy = {
        .bounds = {
            .temperature_min_c = -20, .temperature_max_c = 75,
            .ch0_min_mv = 300, .ch0_max_mv = 800,
            .ch1_min_mv = 300, .ch1_max_mv = 800,
            .ch2_abs_max_mv = 50, .max_stack_spread_mv = 100,
        },
        .max_age_us = BZM_TELEMETRY_MAX_AGE_US,
        .ch2_confirm_samples = 3,
    };
    bzm_bringup_report_t report;
    if (bzm_driver_start(state, &policy, &report) != BZM_BRINGUP_GOOD) {
        BZM_LOGE(TAG, "Startup failed: %s ASIC=0x%02x reg=0x%02x",
                 bzm_bringup_reason_name(report.reason), report.asic_id, report.register_offset);
        return 0;
    }
    bzm_device_record_frequency(state, BZM_FREQUENCY_POWER_ON_MHZ);
    bzm_driver_set_dispatch_authorizer(device_io_allowed, NULL);
    return BZM_BRINGUP_ASIC_COUNT;
}

void bzm_driver_set_frequency(bzm_device_t *state)
{
    bzm_bringup_state_t asic;
    if (!bzm_driver_get_state(&asic) || !asic.running) return;
    bzm_frequency_target_t target;
    if (!bzm_frequency_resolve_target(state->target_frequency_mhz, &target)) return;
    while (fabsf(asic.clock_mhz - target.actual_mhz) >= 0.001f) {
        float delta = target.actual_mhz - asic.clock_mhz;
        float next = asic.clock_mhz + fmaxf(-BZM_FREQUENCY_RAMP_STEP_MHZ,
                                           fminf(BZM_FREQUENCY_RAMP_STEP_MHZ, delta));
        float domains[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT];
        for (size_t chip = 0; chip < BZM_BRINGUP_ASIC_COUNT; ++chip)
            for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) domains[chip][pll] = next;
        bzm_bringup_report_t report;
        if (bzm_driver_step_frequency_domains((const float (*)[BZM_BRINGUP_PLL_COUNT])domains, false, &report, &asic.clock_mhz) != BZM_BRINGUP_GOOD) {
            BZM_LOGE(TAG, "Frequency change failed: %s", bzm_bringup_reason_name(report.reason));
            device_fault(state, "BZM frequency change failed");
            bzm_driver_hold_reset();
            bzm_control_status_t status;
            bzm_control_safe_off(&status);
            return;
        }
        bzm_device_record_frequency(state, asic.clock_mhz);
        bzm_sleep_ms(100);
    }
}
