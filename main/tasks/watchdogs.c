#include <math.h>
#include <stdio.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "connect.h"
#include "global_state.h"
#include "stratum_task.h"
#include "watchdogs.h"

static const char * TAG = "watchdog";

#define TICK_MS 1000
#define NOINIT_MAGIC 0x57444f47 // "WDOG"

// Survives the watchdog's own esp_restart(), so a restart can't reset the
// hourly budget or the counters. Lost on a power cycle.
typedef struct {
    uint32_t magic;
    uint8_t recoveries;
    uint8_t last_action;
    bool restarted;             // this boot is the watchdog's own restart
    uint32_t asic_reinits;
    uint32_t asic_restarts;
} watchdog_noinit_t;

static __NOINIT_ATTR watchdog_noinit_t noinit;

static volatile uint32_t asic_results = 0;
static wd_asic_t asic_wd;
static wd_pool_t pool_wd;
static watchdog_stats_t stats;
static uint64_t last_tick_ms = 0;

void WATCHDOGS_note_asic_result(void)
{
    asic_results++;
}

static uint64_t now_ms(void)
{
    return (uint64_t) (esp_timer_get_time() / 1000);
}

void WATCHDOGS_init(GlobalState * GLOBAL_STATE)
{
    (void) GLOBAL_STATE;
    if (noinit.magic != NOINIT_MAGIC) {
        noinit.magic = NOINIT_MAGIC;
        noinit.recoveries = 0;
        noinit.last_action = WD_ASIC_NONE;
        noinit.restarted = false;
        noinit.asic_reinits = 0;
        noinit.asic_restarts = 0;
    }
    stats.asic_reinits = noinit.asic_reinits;
    stats.asic_restarts = noinit.asic_restarts;
    if (noinit.restarted) {
        noinit.restarted = false;
        snprintf(stats.last_trigger, sizeof(stats.last_trigger), "ASIC: restarted, no results after re-init");
    }
    wd_asic_init(&asic_wd, now_ms(), noinit.recoveries, (wd_asic_action_t) noinit.last_action);
    wd_pool_init(&pool_wd);
}

static bool at_target_frequency(const PowerManagementModule * power)
{
    float target = power->frequency_value;
    if (target <= 0.0f) {
        return false;
    }
    float tolerance = fmaxf(5.0f, target * 0.02f);
    return fabsf(power->actual_frequency - target) <= tolerance;
}

static void pool_tick(GlobalState * GLOBAL_STATE, uint64_t now)
{
    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;
    bool active = !GLOBAL_STATE->SELF_TEST_MODULE.is_active && !sys->mining_paused && !sys->hardware_fault
               && !sys->pools_unavailable && wifi_is_connected();
    uint16_t pool_idx = sys->is_using_fallback ? sys->secondary_pool_index : sys->primary_pool_index;

    const PoolSession * session = &sys->pool_sessions[pool_idx < MAX_POOLS ? pool_idx : 0];
    wd_pool_action_t action = wd_pool_step(&pool_wd, now, active, session->work_received, session->shares_submitted,
                                           session->shares_accepted + session->shares_rejected);
    if (action == WD_POOL_NONE) {
        return;
    }

    stats.pool_reconnects++;
    if (action == WD_POOL_NO_WORK) {
        ESP_LOGW(TAG, "Pool %u sent no work for %d s, reconnecting", pool_idx, WD_POOL_NO_WORK_MS / 1000);
        snprintf(stats.last_trigger, sizeof(stats.last_trigger), "Pool %u: no work for %d s", pool_idx, WD_POOL_NO_WORK_MS / 1000);
    } else {
        ESP_LOGW(TAG, "Pool %u answered no share for %d s, reconnecting", pool_idx, WD_POOL_NO_REPLY_MS / 1000);
        snprintf(stats.last_trigger, sizeof(stats.last_trigger), "Pool %u: no share replies for %d s", pool_idx, WD_POOL_NO_REPLY_MS / 1000);
    }
    stratum_report_stalled_pool(pool_idx);
}

wd_asic_action_t WATCHDOGS_tick(GlobalState * GLOBAL_STATE)
{
    uint64_t now = now_ms();
    if (now - last_tick_ms < TICK_MS) {
        return WD_ASIC_NONE;
    }
    last_tick_ms = now;

    pool_tick(GLOBAL_STATE, now);

    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    wd_asic_input_t in = {
        .running = GLOBAL_STATE->ASIC_initalized && !GLOBAL_STATE->SELF_TEST_MODULE.is_active && !sys->mining_paused
                && !sys->overheat_mode && !sys->hardware_fault && !sys->pools_unavailable && at_target_frequency(power),
        .has_work = GLOBAL_STATE->has_work,
        .results = asic_results,
        .window_ms = wd_asic_window_ms(power->expected_hashrate, GLOBAL_STATE->current_difficulty_power),
    };
    wd_asic_action_t action = wd_asic_step(&asic_wd, now, &in);
    noinit.recoveries = asic_wd.recoveries;
    noinit.last_action = asic_wd.last_action;

    switch (action) {
        case WD_ASIC_REINIT:
            stats.asic_reinits = ++noinit.asic_reinits;
            ESP_LOGW(TAG, "No results from the ASIC chain for %lu s, re-initialising (recovery %u of %d this hour)",
                     (unsigned long) (in.window_ms / 1000), asic_wd.recoveries, WD_ASIC_MAX_RECOVERIES);
            snprintf(stats.last_trigger, sizeof(stats.last_trigger), "ASIC: no results for %lu s, re-initialised",
                     (unsigned long) (in.window_ms / 1000));
            break;
        case WD_ASIC_RESTART:
            ESP_LOGE(TAG, "Still no results from the ASIC chain after a re-init, restarting");
            break;
        case WD_ASIC_GIVE_UP:
            ESP_LOGE(TAG, "No results from the ASIC chain and %d recoveries used this hour, stopping",
                     WD_ASIC_MAX_RECOVERIES);
            snprintf(stats.last_trigger, sizeof(stats.last_trigger), "ASIC: no results, recoveries used up");
            break;
        default:
            break;
    }
    return action;
}

void WATCHDOGS_before_restart(void)
{
    stats.asic_restarts = ++noinit.asic_restarts;
    noinit.recoveries = asic_wd.recoveries;
    noinit.last_action = WD_ASIC_RESTART;
    noinit.restarted = true;
}

const watchdog_stats_t * WATCHDOGS_get_stats(void)
{
    return &stats;
}
