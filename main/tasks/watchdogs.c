#include <math.h>
#include <stdio.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "connect.h"
#include "global_state.h"
#include "pool.h"
#include "stratum_task.h"
#include "watchdogs.h"

static const char * TAG = "watchdog";

#define NOINIT_MAGIC 0x57444f47 // "WDOG"

// Survives the watchdog's own esp_restart(), so a restart can't reset the
// recovery budget. Lost on a power cycle.
typedef struct {
    uint32_t magic;
    uint8_t recoveries;
    uint8_t last_action;
    bool restarted;             // this boot is the watchdog's own restart
} watchdog_noinit_t;

static __NOINIT_ATTR watchdog_noinit_t noinit;

static volatile uint32_t asic_results = 0;
static wd_asic_t asic_wd;
static wd_pool_t pool_wd;
static int64_t next_tick_us = 0;

void WATCHDOGS_note_asic_result(void)
{
    asic_results++;
}

void WATCHDOGS_init(void)
{
    if (noinit.magic != NOINIT_MAGIC) {
        noinit.magic = NOINIT_MAGIC;
        noinit.recoveries = 0;
        noinit.last_action = WD_ASIC_NONE;
        noinit.restarted = false;
    }
    if (noinit.restarted) {
        noinit.restarted = false;
        ESP_LOGW(TAG, "Started after a watchdog restart (recovery %u of %d)", noinit.recoveries,
                 WD_ASIC_MAX_RECOVERIES);
    }
    wd_asic_init(&asic_wd, noinit.recoveries, (wd_asic_action_t) noinit.last_action);
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

static void pool_tick(GlobalState * GLOBAL_STATE)
{
    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;
    bool active = !GLOBAL_STATE->SELF_TEST_MODULE.is_active && !sys->mining_paused && !sys->hardware_fault
               && !sys->pools_unavailable && wifi_is_connected();
    uint16_t pool_idx = sys->is_using_fallback ? sys->secondary_pool_index : sys->primary_pool_index;

    const PoolSession * session = &sys->pool_sessions[pool_idx < MAX_POOLS ? pool_idx : 0];
    wd_pool_action_t action = wd_pool_step(&pool_wd, active, session->work_received, session->shares_submitted,
                                           session->shares_accepted + session->shares_rejected);
    if (action == WD_POOL_NO_WORK) {
        ESP_LOGW(TAG, "Pool %u sent no work for %d s, reconnecting", pool_idx, WD_POOL_NO_WORK_S);
    } else if (action == WD_POOL_NO_REPLIES) {
        ESP_LOGW(TAG, "Pool %u answered no share for %d s, reconnecting", pool_idx, WD_POOL_NO_REPLY_S);
    } else {
        return;
    }
    stratum_report_stalled_pool(pool_idx);
}

wd_asic_action_t WATCHDOGS_tick(GlobalState * GLOBAL_STATE)
{
    // The power management loop runs every 100 ms; the watchdogs step once a second
    int64_t now_us = esp_timer_get_time();
    if (now_us < next_tick_us) {
        return WD_ASIC_NONE;
    }
    next_tick_us = now_us + 1000 * 1000;

    pool_tick(GLOBAL_STATE);

    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    const PoolSession * session = POOL_get_active_session(GLOBAL_STATE);
    wd_asic_input_t in = {
        .running = GLOBAL_STATE->ASIC_initalized && !GLOBAL_STATE->SELF_TEST_MODULE.is_active && !sys->mining_paused
                && !sys->overheat_mode && !sys->hardware_fault && !sys->pools_unavailable && at_target_frequency(power),
        .has_work = session != NULL && session->work_received > 0,
        .results = asic_results,
        .hashrate_1m = sys->hashrate_1m,
        .window_s = wd_asic_window_s(power->expected_hashrate, sys->current_difficulty_power),
    };
    wd_asic_action_t action = wd_asic_step(&asic_wd, &in);
    noinit.recoveries = asic_wd.recoveries;
    noinit.last_action = asic_wd.last_action;

    if (action == WD_ASIC_NONE) {
        return action;
    }

    // Say which signal fired, so field reports show which one catches it
    char signal[64];
    if (asic_wd.reason == WD_ASIC_REASON_FROZEN_HASHRATE) {
        snprintf(signal, sizeof(signal), "1m hashrate frozen at %.2f GH/s for %d s", in.hashrate_1m, WD_ASIC_FROZEN_S);
    } else {
        snprintf(signal, sizeof(signal), "no results for %lu s", (unsigned long) in.window_s);
    }

    switch (action) {
        case WD_ASIC_REINIT:
            ESP_LOGW(TAG, "ASIC chain: %s, re-initialising (recovery %u of %d)", signal, asic_wd.recoveries,
                     WD_ASIC_MAX_RECOVERIES);
            break;
        case WD_ASIC_RESTART:
            ESP_LOGE(TAG, "ASIC chain: %s again after a re-init, restarting", signal);
            break;
        case WD_ASIC_GIVE_UP:
            ESP_LOGE(TAG, "ASIC chain: %s and all %d recoveries used, stopping", signal, WD_ASIC_MAX_RECOVERIES);
            break;
        default:
            break;
    }
    return action;
}

void WATCHDOGS_before_restart(void)
{
    noinit.recoveries = asic_wd.recoveries;
    noinit.last_action = WD_ASIC_RESTART;
    noinit.restarted = true;
}
