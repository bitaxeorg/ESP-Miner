#include <stdint.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "autotune_task.h"

static const char * TAG = "autotune";

// --- Tuning behaviour: three selectable profiles (NVS_CONFIG_AUTOTUNE_PROFILE) ---
//
// Everything that stays the same across profiles (timing, sample sizes,
// hard safety ceilings) is still a plain #define below. Everything that
// defines "how close to the edge" the tuner is willing to sit -- thermal
// margin, instability tolerances, and whether it reaches for beyond-spec
// or an extra-voltage rescue at all -- lives in this table instead.
typedef struct {
    const char * name;
    float temp_margin_c;              // safety margin below the reactive throttle cutoff
    float reject_rate_max;            // vendor-table reject-rate tolerance
    float error_percentage_max;       // vendor-table ASIC-error tolerance
    float extended_reject_rate_max;   // beyond-spec reject-rate tolerance
    float extended_error_percentage_max; // beyond-spec ASIC-error tolerance
    bool allow_beyond_spec;           // ever climb past the vendor table, even if unlocked
    bool allow_voltage_rescue;        // try extra voltage to save a failing frequency step
} AutotuneProfileConfig;

static const AutotuneProfileConfig AUTOTUNE_PROFILES[] = {
    [AUTOTUNE_PROFILE_ECO] = {
        .name = "eco",
        .temp_margin_c = 10.0f,
        .reject_rate_max = 0.02f,
        .error_percentage_max = 1.0f,
        .extended_reject_rate_max = 0.01f,
        .extended_error_percentage_max = 0.5f,
        .allow_beyond_spec = false,
        .allow_voltage_rescue = false, // accept a lower ceiling rather than spend extra heat/power to hold one
    },
    [AUTOTUNE_PROFILE_BALANCED] = {
        .name = "balanced",
        .temp_margin_c = 5.0f,
        .reject_rate_max = 0.03f,
        .error_percentage_max = 2.0f,
        .extended_reject_rate_max = 0.015f,
        .extended_error_percentage_max = 1.0f,
        .allow_beyond_spec = false, // full vendor-table speed, but stops there even if beyond-spec is unlocked
        .allow_voltage_rescue = true,
    },
    [AUTOTUNE_PROFILE_AGGRESSIVE] = {
        .name = "aggressive",
        .temp_margin_c = 2.0f,
        .reject_rate_max = 0.03f,
        .error_percentage_max = 2.0f,
        .extended_reject_rate_max = 0.015f,
        .extended_error_percentage_max = 1.0f,
        .allow_beyond_spec = true, // if custom settings are unlocked, keeps climbing past the vendor table
        .allow_voltage_rescue = true,
    },
};
#define AUTOTUNE_PROFILE_COUNT (sizeof(AUTOTUNE_PROFILES) / sizeof(AUTOTUNE_PROFILES[0]))

static const AutotuneProfileConfig * get_active_profile(void)
{
    uint16_t idx = nvs_config_get_u16(NVS_CONFIG_AUTOTUNE_PROFILE);
    if (idx >= AUTOTUNE_PROFILE_COUNT) {
        idx = AUTOTUNE_PROFILE_BALANCED; // unexpected NVS value -- fall back to the safe default
    }
    return &AUTOTUNE_PROFILES[idx];
}

// How often we sample temperature/state. Kept short so an in-window temp spike
// gets a reaction quickly, since this profile intentionally runs with a thin margin.
#define TICK_MS 5000

// How long we let a given frequency/voltage step run before judging it.
// Long enough to gather a meaningful number of shares, short enough that the
// tuner is responsive to changing ambient/cooling conditions (continuous mode).
#define EVAL_WINDOW_MS (90 * 1000)

// Need at least this many accepted+rejected shares in a window before trusting
// the reject rate; otherwise we extend the window rather than judge on noise.
#define MIN_SAMPLE_SHARES 15

// Voltage is adjusted in small, continuous mV steps rather than jumping
// between the vendor table's handful of discrete entries (which can be
// 40-60mV apart). The voltage regulator itself has no problem with
// arbitrary values in between -- the vendor table is a firmware-level
// convenience, not a hardware restriction -- so this is purely a precision
// improvement: the tuner can settle much closer to the true minimum stable
// voltage for a given frequency, instead of overshooting to the next
// coarse step every time. Voltage is still hard-clamped to the vendor
// table's own min/max, in every profile, regardless of this finer step size.
#define VOLTAGE_STEP_MV 10.0f

// Thermal margin, reject-rate tolerance, ASIC-error tolerance, and whether
// beyond-spec/voltage-rescue are allowed at all now come from the active
// AutotuneProfileConfig (see AUTOTUNE_PROFILES above) instead of being
// fixed here -- that's what makes eco/balanced/aggressive behave
// differently. The reactive throttle cutoff itself (PM_THROTTLE_TEMP) is
// unaffected by any profile: it's power_management_task's hard backstop,
// not something this tuner controls.

// --- Beyond-spec extension (opt-in only, gated on NVS_CONFIG_OVERCLOCK_ENABLED) ---
//
// Once the tuner reaches the top of the vendor-tested frequency table AND the
// user has explicitly unlocked "custom settings" in the UI (the same signal
// the UI already uses to expose raw frequency/voltage entry), the tuner is
// allowed to keep exploring upward in small, conservative steps.
//
// Voltage is deliberately NOT extended past the vendor table under any
// circumstances -- overvolting is the more likely path to permanent chip
// damage. Frequency alone, without additional voltage, tends to fail as
// instability (rejected shares) well before it fails as damage, which is
// why only frequency is allowed to explore past vendor data here.
#define EXTENDED_STEP_MHZ 5.0f

// Hard, non-configurable ceiling even in beyond-spec mode: this multiplier
// applies to the vendor table's own highest frequency. This is the default
// fallback for anyone who hasn't set their own verified ceiling below.
#define EXTENDED_MAX_MULTIPLIER 1.15f

// A personally-verified ceiling the *person* has typed in, via
// NVS_CONFIG_AUTOTUNE_MAX_MHZ -- not a value baked into the firmware for a
// whole chip family. Silicon varies between individual units of the same
// chip ("silicon lottery"), so a value one person confirmed stable on their
// own hardware isn't safe to assume for everyone else running this same
// firmware. Defaults to 0 (unset), which falls back to the generic
// percentage-based ceiling below.
static float get_user_verified_ceiling_mhz(void)
{
    return nvs_config_get_float(NVS_CONFIG_AUTOTUNE_MAX_MHZ);
}

// Beyond-spec steps are unvalidated, so we require a longer, cleaner window
// before trusting a step as stable than the standard vendor-table climbing
// uses. The reject-rate/error tolerances themselves come from the active
// profile (see extended_reject_rate_max / extended_error_percentage_max).
#define EXTENDED_MIN_SAMPLE_SHARES 30

// After this many consecutive failures trying to climb past roughly the same
// beyond-spec frequency, stop retrying it and treat that point as the
// practical ceiling for this session -- letting the tuner move on to voltage
// optimization instead of retrying a step that clearly isn't going to work.
#define EXTENDED_FAIL_LIMIT 2

// Hard ceiling for beyond-spec frequency exploration: the person's own
// verified value if they've set one, otherwise the generic percentage-based
// fallback -- further capped by a soft ceiling if the tuner has recently
// given up trying to climb past a specific point.
static float get_extended_ceiling_mhz(const uint16_t * freq_options, int freq_option_count,
                                       float soft_ceiling_mhz)
{
    float vendor_max_freq = (float) freq_options[freq_option_count - 1];
    float known_ceiling = get_user_verified_ceiling_mhz();
    float hard_ceiling = known_ceiling > 0.0f ? known_ceiling : (vendor_max_freq * EXTENDED_MAX_MULTIPLIER);
    if (soft_ceiling_mhz > 0.0f && soft_ceiling_mhz < hard_ceiling) {
        return soft_ceiling_mhz;
    }
    return hard_ceiling;
}

// True if there's still room for one more beyond-spec climb step without
// going over the ceiling (user-verified, percentage-based, or the
// session's soft ceiling from repeated failures -- whichever applies).
static bool next_extended_step_within_ceiling(AutotuneModule * at, const uint16_t * freq_options, int freq_option_count)
{
    float vendor_max_freq = (float) freq_options[freq_option_count - 1];
    float current = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : vendor_max_freq;
    float ceiling = get_extended_ceiling_mhz(freq_options, freq_option_count, at->extended_soft_ceiling_mhz);
    return (current + EXTENDED_STEP_MHZ) <= ceiling;
}

static int find_index(const uint16_t * options, uint16_t value)
{
    if (!options) {
        return 0;
    }
    int idx = 0;
    int best_idx = 0;
    uint16_t best_diff = UINT16_MAX;
    while (options[idx] != 0) {
        uint16_t diff = options[idx] > value ? options[idx] - value : value - options[idx];
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = idx;
        }
        idx++;
    }
    return best_idx;
}

static int count_options(const uint16_t * options)
{
    if (!options) {
        return 0;
    }
    int count = 0;
    while (options[count] != 0) {
        count++;
    }
    return count;
}

static float clamp_voltage(float voltage_mv, float min_voltage_mv, float max_voltage_mv)
{
    if (voltage_mv < min_voltage_mv) {
        return min_voltage_mv;
    }
    if (voltage_mv > max_voltage_mv) {
        return max_voltage_mv;
    }
    return voltage_mv;
}

// Scales how far we are into the frequency table onto the voltage range, so
// voltage can be raised proactively in step with frequency instead of only
// reactively once a step actually fails. Reaching a given frequency without
// first burning through several failed-attempt-then-rescue cycles (each a
// full ~90s evaluation window) gets there noticeably faster. This doesn't
// compromise final efficiency: the undervolt-optimization phase that runs
// once the ceiling is reached will still shave voltage back down, in the
// same fine 10mV steps, to whatever the minimum actually needed turns out
// to be.
static float proportional_voltage_mv(int freq_step_index, int freq_option_count, float min_voltage_mv, float max_voltage_mv)
{
    if (freq_option_count <= 1) {
        return min_voltage_mv;
    }
    float progress = (float) freq_step_index / (float) (freq_option_count - 1);
    return min_voltage_mv + progress * (max_voltage_mv - min_voltage_mv);
}

static void apply_step(GlobalState * GLOBAL_STATE, const uint16_t * freq_options,
                        int freq_idx, float voltage_mv, float extended_freq_mhz)
{
    // extended_freq_mhz > 0 means we're operating past the vendor table --
    // that value wins over whatever the table index says.
    float new_freq = extended_freq_mhz > 0.0f ? extended_freq_mhz : (float) freq_options[freq_idx];
    uint16_t new_volt = (uint16_t) (voltage_mv + 0.5f);

    ESP_LOGI(TAG, "Applying step: %.0f MHz @ %u mV%s", new_freq, new_volt,
             extended_freq_mhz > 0.0f ? " (beyond vendor spec)" : "");

    nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, new_freq);
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, new_volt);
    // power_management_task polls these NVS values on its own ~100ms loop and
    // applies them to the ASIC; we don't touch the hardware directly here so
    // there's a single owner of "what the ASIC is currently told to do".
}

static void resync_from_nvs(AutotuneModule * at, const uint16_t * freq_options, int freq_option_count)
{
    float current_freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    at->freq_step_index = find_index(freq_options, (uint16_t) current_freq);
    at->voltage_mv = (float) nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);

    float vendor_max_freq = (float) freq_options[freq_option_count - 1];
    at->extended_freq_mhz = (current_freq > vendor_max_freq + 0.5f) ? current_freq : 0.0f;
    at->extended_soft_ceiling_mhz = 0.0f;
    at->extended_freq_consecutive_fails = 0;
    at->last_action = AUTOTUNE_ACTION_NONE;
}

// Undo whatever the last applied step was, because it turned out unstable
// (either the reject rate over the window, or a mid-window temperature
// spike). Reverting the *specific* thing that changed -- rather than always
// assuming "lower the frequency" -- is what makes the undervolt trials and
// the extra-voltage-for-stability trials safe to attempt at all.
static void revert_last_action(AutotuneModule * at, const uint16_t * freq_options, int freq_option_count,
                                float min_voltage_mv, float max_voltage_mv, bool allow_voltage_rescue)
{
    // A frequency step that failed on rejected shares (not heat) sometimes
    // just needs a bit more voltage headroom rather than being abandoned
    // outright. Only offer that rescue for reject-rate-based instability --
    // never for a temperature-driven revert, since adding voltage there
    // would work against the very problem we're reacting to.
    if (allow_voltage_rescue && at->last_action == AUTOTUNE_ACTION_FREQ_UP &&
        at->voltage_mv < max_voltage_mv - 0.5f) {
        at->voltage_mv = clamp_voltage(at->voltage_mv + VOLTAGE_STEP_MV, min_voltage_mv, max_voltage_mv);
        at->last_action = AUTOTUNE_ACTION_VOLT_UP;
        return;
    }

    switch (at->last_action) {
        case AUTOTUNE_ACTION_VOLT_DOWN:
            // The undervolt trial didn't hold -- go back up to the last
            // known-good voltage for this frequency.
            at->voltage_mv = clamp_voltage(at->voltage_mv + VOLTAGE_STEP_MV, min_voltage_mv, max_voltage_mv);
            break;
        case AUTOTUNE_ACTION_VOLT_UP:
            // Even extra voltage didn't stabilize this frequency -- undo the
            // voltage bump AND give up on this frequency step. If we were
            // exploring beyond-spec, that "step" lives in extended_freq_mhz,
            // not freq_step_index (which stays pinned at the vendor max
            // throughout beyond-spec exploration) -- back off the right one.
            at->voltage_mv = clamp_voltage(at->voltage_mv - VOLTAGE_STEP_MV, min_voltage_mv, max_voltage_mv);
            if (at->extended_freq_mhz > 0.0f) {
                at->extended_freq_mhz -= EXTENDED_STEP_MHZ;
                if (at->extended_freq_mhz <= (float) freq_options[freq_option_count - 1] + 0.5f) {
                    at->extended_freq_mhz = 0.0f; // back into vendor-tested territory
                }
            } else if (at->freq_step_index > 0) {
                at->freq_step_index--;
            }
            break;
        case AUTOTUNE_ACTION_FREQ_UP:
        case AUTOTUNE_ACTION_NONE:
        default:
            if (at->extended_freq_mhz > 0.0f) {
                at->extended_freq_mhz -= EXTENDED_STEP_MHZ;
                if (at->extended_freq_mhz <= (float) freq_options[freq_option_count - 1] + 0.5f) {
                    at->extended_freq_mhz = 0.0f; // back into vendor-tested territory
                }
            } else if (at->freq_step_index > 0) {
                at->freq_step_index--;
            } else if (at->voltage_mv > min_voltage_mv + 0.5f) {
                at->voltage_mv = clamp_voltage(at->voltage_mv - VOLTAGE_STEP_MV, min_voltage_mv, max_voltage_mv);
            }
            break;
    }
    at->last_action = AUTOTUNE_ACTION_NONE;
}

// True if the live NVS frequency/voltage no longer match what the tuner
// itself last commanded -- meaning something external (almost always the
// person, via the settings page) changed it while autotune was running.
static bool settings_changed_externally(AutotuneModule * at, const uint16_t * freq_options)
{
    float expected_freq = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : (float) freq_options[at->freq_step_index];

    float actual_freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    float actual_volt = (float) nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);

    bool freq_matches = fabsf(actual_freq - expected_freq) < 0.5f;
    // Voltage is stored as an integer mV in NVS, so allow for that rounding
    // when comparing against our own float-tracked value.
    bool volt_matches = fabsf(actual_volt - at->voltage_mv) < 1.0f;

    return !freq_matches || !volt_matches;
}

// Called on every reverted/backed-off step. If the step we just gave up on
// was a beyond-spec frequency climb, count it -- after enough consecutive
// failures at roughly the same point, get_extended_ceiling_mhz() will stop
// letting the tuner retry it (see extended_soft_ceiling_mhz) and it'll move
// on to voltage optimization instead. Any other kind of revert (an
// undervolt trial, or a vendor-table-only step) resets the streak, since
// it's not part of a beyond-spec climb attempt.
static void track_extended_climb_failure(AutotuneModule * at, const char * reason)
{
    if (at->last_action != AUTOTUNE_ACTION_FREQ_UP || at->extended_freq_mhz <= 0.0f) {
        at->extended_freq_consecutive_fails = 0;
        return;
    }

    at->extended_freq_consecutive_fails++;
    if (at->extended_freq_consecutive_fails >= EXTENDED_FAIL_LIMIT) {
        // extended_freq_mhz still holds the failing value here; the last
        // known-good level is one step below it.
        at->extended_soft_ceiling_mhz = at->extended_freq_mhz - EXTENDED_STEP_MHZ;
        ESP_LOGI(TAG, "Giving up climbing past %.0f MHz after %d failed attempts (%s), moving to voltage optimization",
                 at->extended_soft_ceiling_mhz, at->extended_freq_consecutive_fails, reason);
    }
}

// The pool can reject a share for reasons that have nothing to do with
// whether the current frequency/voltage is stable -- most commonly
// "Duplicate share", which happens more often at low frequency/low pool
// difficulty simply because the chip finds qualifying results faster
// relative to how often new work arrives. Counting these against the tuner's
// stability judgement would make even the lowest, safest settings look
// unstable and give it nowhere left to fall back to. We look this reason up
// by name in the existing rejected-reason breakdown and exclude it.
static uint32_t get_rejected_reason_count(SystemModule * sys_module, const char * reason)
{
    for (int i = 0; i < sys_module->rejected_reason_stats_count; i++) {
        if (strncmp(sys_module->rejected_reason_stats[i].message, reason, sizeof(sys_module->rejected_reason_stats[i].message) - 1) == 0) {
            return sys_module->rejected_reason_stats[i].count;
        }
    }
    return 0;
}

void AUTOTUNE_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    AutotuneModule * at = &GLOBAL_STATE->AUTOTUNE_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * pm = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    const uint16_t * freq_options = GLOBAL_STATE->DEVICE_CONFIG.family.asic.frequency_options;
    const uint16_t * volt_options = GLOBAL_STATE->DEVICE_CONFIG.family.asic.voltage_options;
    int freq_option_count = count_options(freq_options);
    int volt_option_count = count_options(volt_options);

    if (freq_option_count == 0 || volt_option_count == 0) {
        ESP_LOGE(TAG, "No frequency/voltage option table for this ASIC, autotune cannot run");
        vTaskDelete(NULL);
        return;
    }

    // Voltage is tuned continuously (see VOLTAGE_STEP_MV) between the
    // vendor table's own lowest and highest entries -- those two values are
    // still the hard bounds in every profile, we just no longer skip
    // everything in between.
    float min_voltage_mv = (float) volt_options[0];
    float max_voltage_mv = (float) volt_options[volt_option_count - 1];

    at->state = AUTOTUNE_STATE_IDLE;
    at->step_downs_total = 0;
    at->step_ups_total = 0;

    uint64_t window_start_accepted = 0;
    uint64_t window_start_rejected = 0;
    uint32_t window_start_duplicate = 0;
    int64_t window_start_ms = 0;
    float window_max_temp = 0.0f;
    bool window_open = false;
    // Tracks the disabled->enabled transition so we only reset to the
    // bottom of the table once per "session", not on every loop tick.
    bool was_enabled = false;

    while (1) {
        vTaskDelay(TICK_MS / portTICK_PERIOD_MS);

        if (GLOBAL_STATE->SELF_TEST_MODULE.is_finished) {
            ESP_LOGI(TAG, "Stopped");
            vTaskDelete(NULL);
            return;
        }

        bool enabled = nvs_config_get_bool(NVS_CONFIG_AUTOTUNE_ENABLED);
        if (!enabled) {
            at->state = AUTOTUNE_STATE_IDLE;
            window_open = false;
            was_enabled = false;
            continue;
        }

        // Re-read every tick rather than caching at startup: the person can
        // switch profiles while autotune is running, and it should take
        // effect on the next evaluation rather than requiring a re-enable.
        const AutotuneProfileConfig * profile = get_active_profile();
        at->active_profile = (AutotuneProfile) (profile - AUTOTUNE_PROFILES);
        float temp_ceiling_c = PM_THROTTLE_TEMP - profile->temp_margin_c;

        if (!was_enabled) {
            // Just (re)enabled. Start from the bottom of the vendor table
            // for both frequency and voltage, and let the normal climb take
            // it from there -- frequency climbs step by step, and voltage
            // only goes up when a frequency step actually needs the rescue
            // to stabilize (see revert_last_action), or proactively in step
            // with frequency (see proportional_voltage_mv). This tends
            // toward a near-minimal voltage for whatever frequency is
            // reached, rather than starting at whatever voltage happened to
            // be set before and only ever shaving it down afterward.
            at->freq_step_index = 0;
            at->voltage_mv = min_voltage_mv;
            at->extended_freq_mhz = 0.0f;
            at->extended_soft_ceiling_mhz = 0.0f;
            at->extended_freq_consecutive_fails = 0;
            at->last_action = AUTOTUNE_ACTION_NONE;
            apply_step(GLOBAL_STATE, freq_options, 0, at->voltage_mv, 0.0f);
            ESP_LOGI(TAG, "Autotune enabled (%s profile): starting from the bottom (%.0f MHz @ %.0f mV) and climbing",
                     profile->name, (float) freq_options[0], at->voltage_mv);
            was_enabled = true;
            window_open = false;
            continue;
        }

        bool blocked = sys_module->overheat_mode || sys_module->mining_paused ||
                        sys_module->hardware_fault || sys_module->pools_unavailable ||
                        !GLOBAL_STATE->ASIC_initalized;

        if (blocked) {
            // Something else (most likely the reactive overheat-protection in
            // power_management_task) has taken over. Don't fight it: just
            // resync our own idea of the current step to whatever is actually
            // configured now, and wait until things are calm again.
            at->state = AUTOTUNE_STATE_PAUSED;
            window_open = false;
            resync_from_nvs(at, freq_options, freq_option_count);
            continue;
        }

        if (settings_changed_externally(at, freq_options)) {
            // The person (or something else) changed frequency/voltage
            // directly, outside the tuner. Treat that as a deliberate new
            // starting point rather than silently overwriting it on the next
            // action -- resync to it and start a fresh evaluation window.
            ESP_LOGI(TAG, "Frequency/voltage changed externally, adopting new starting point");
            resync_from_nvs(at, freq_options, freq_option_count);
            window_open = false;
        }

        float chip_temp = pm->chip_temp_avg;
        if (pm->chip_temp2_avg > chip_temp) {
            chip_temp = pm->chip_temp2_avg;
        }

        if (!window_open) {
            window_start_accepted = sys_module->shares_accepted;
            window_start_rejected = sys_module->shares_rejected;
            window_start_duplicate = get_rejected_reason_count(sys_module, "Duplicate share");
            window_start_ms = esp_timer_get_time() / 1000;
            window_max_temp = chip_temp;
            window_open = true;
            at->state = AUTOTUNE_STATE_WARMING;
            continue;
        }

        if (chip_temp > window_max_temp) {
            window_max_temp = chip_temp;
        }
        at->max_temp_seen_this_window = window_max_temp;

        // Fast-path safety check: don't wait for the window to complete if
        // we're already over the ceiling. Aggressive profile rides close to
        // this line; eco/balanced keep a much wider margin (see AUTOTUNE_PROFILES).
        if (window_max_temp > temp_ceiling_c) {
            ESP_LOGW(TAG, "Temp %.1fC over ceiling %.1fC mid-window, stepping down early",
                     window_max_temp, temp_ceiling_c);

            track_extended_climb_failure(at, "heat");

            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, false);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            window_open = false;
            continue;
        }

        // Fast-path safety check on the ASIC's own on-chip error rate --
        // see the profile's error_percentage_max comment for why this matters and why
        // it isn't held for the full evaluation window. Unlike the
        // temperature check, a voltage rescue is worth trying here first
        // (if this profile allows it), since (like rejected shares) this
        // can reflect insufficient voltage headroom rather than heat.
        float error_threshold = at->extended_freq_mhz > 0.0f ? profile->extended_error_percentage_max : profile->error_percentage_max;
        if (sys_module->error_percentage > error_threshold) {
            ESP_LOGW(TAG, "ASIC error rate %.1f%% over threshold %.1f%% mid-window, reverting",
                     sys_module->error_percentage, error_threshold);

            track_extended_climb_failure(at, "ASIC errors");

            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, profile->allow_voltage_rescue);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            window_open = false;
            continue;
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - window_start_ms < EVAL_WINDOW_MS) {
            continue; // still collecting samples for this step
        }

        uint64_t accepted_delta = sys_module->shares_accepted - window_start_accepted;
        uint64_t rejected_delta_raw = sys_module->shares_rejected - window_start_rejected;

        // "Duplicate share" rejections are a pool/protocol artifact, not a
        // sign that this frequency/voltage is unstable -- see the comment on
        // get_rejected_reason_count(). Exclude them from both the reject
        // count and the sample total so they can't make an otherwise
        // perfectly stable step (including the lowest, safest one) look
        // unstable, which would leave the tuner nowhere to fall back to.
        uint32_t duplicate_delta = get_rejected_reason_count(sys_module, "Duplicate share") - window_start_duplicate;
        uint64_t rejected_delta = (rejected_delta_raw > duplicate_delta) ? (rejected_delta_raw - duplicate_delta) : 0;
        uint64_t total_delta = accepted_delta + rejected_delta;

        // Beyond-spec steps get a stricter bar: more samples required, lower
        // tolerance for rejects, since these values are unvalidated by the vendor.
        int min_samples = at->extended_freq_mhz > 0.0f ? EXTENDED_MIN_SAMPLE_SHARES : MIN_SAMPLE_SHARES;
        float reject_threshold = at->extended_freq_mhz > 0.0f ? profile->extended_reject_rate_max : profile->reject_rate_max;

        if ((int) total_delta < min_samples) {
            // Not enough shares yet to trust the reject rate (e.g. high pool
            // difficulty). Keep the window open a bit longer instead of
            // judging on a handful of samples.
            continue;
        }

        float reject_rate = (float) rejected_delta / (float) total_delta;
        at->last_reject_rate = reject_rate;

        bool unstable = reject_rate > reject_threshold;

        if (unstable) {
            ESP_LOGW(TAG, "Reject rate %.1f%% over threshold at %d/%.0fMHz @ %.0fmV%s, reverting",
                     reject_rate * 100.0f, at->freq_step_index, (float) freq_options[at->freq_step_index], at->voltage_mv,
                     at->extended_freq_mhz > 0.0f ? " (beyond spec)" : "");

            track_extended_climb_failure(at, "rejected shares");

            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, profile->allow_voltage_rescue);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
        } else if (at->last_action == AUTOTUNE_ACTION_VOLT_UP) {
            // The extra voltage successfully stabilized this frequency step.
            // Keep it, and go straight back to trying to climb frequency
            // further on the next cycle. This frequency level did ultimately
            // work, so it shouldn't count toward "repeatedly failing here".
            at->extended_freq_consecutive_fails = 0;
            at->last_action = AUTOTUNE_ACTION_NONE;
            at->state = AUTOTUNE_STATE_WARMING;
        } else if (at->freq_step_index < freq_option_count - 1) {
            at->freq_step_index++;
            at->last_action = AUTOTUNE_ACTION_FREQ_UP;
            at->step_ups_total++;
            at->state = AUTOTUNE_STATE_WARMING;

            // Proactively bring voltage along for the ride (Eco stays
            // reactive-only, in line with its "accept a lower ceiling
            // rather than spend extra heat/power" philosophy -- see
            // allow_voltage_rescue).
            if (profile->allow_voltage_rescue) {
                float target_voltage = proportional_voltage_mv(at->freq_step_index, freq_option_count, min_voltage_mv, max_voltage_mv);
                if (target_voltage > at->voltage_mv) {
                    at->voltage_mv = clamp_voltage(target_voltage, min_voltage_mv, max_voltage_mv);
                }
            }

            apply_step(GLOBAL_STATE, freq_options, at->freq_step_index, at->voltage_mv, 0.0f);
        } else if (profile->allow_beyond_spec && nvs_config_get_bool(NVS_CONFIG_OVERCLOCK_ENABLED) &&
                   next_extended_step_within_ceiling(at, freq_options, freq_option_count)) {
            // Vendor-table frequency is maxed, this profile allows going
            // further, and custom settings are unlocked -- keep exploring
            // frequency alone past the vendor table, up to a fixed ceiling.
            // Voltage stays exactly where it is; extended mode never
            // pushes voltage further.
            float vendor_max_freq = (float) freq_options[freq_option_count - 1];
            float base = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : vendor_max_freq;

            at->extended_freq_mhz = base + EXTENDED_STEP_MHZ;
            at->extended_freq_consecutive_fails = 0; // made real progress -- past failures no longer apply
            at->last_action = AUTOTUNE_ACTION_FREQ_UP;
            at->step_ups_total++;
            at->state = AUTOTUNE_STATE_BEYOND_SPEC;
            apply_step(GLOBAL_STATE, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
        } else if (at->voltage_mv > min_voltage_mv + 0.5f) {
            // The highest frequency we currently trust is maxed out (vendor
            // ceiling, or the beyond-spec ceiling if that's active) and
            // stable. Try shaving voltage down at this same frequency, in
            // fine 10mV steps -- same hashrate, less heat, less power, if
            // it holds.
            at->voltage_mv = clamp_voltage(at->voltage_mv - VOLTAGE_STEP_MV, min_voltage_mv, max_voltage_mv);
            at->last_action = AUTOTUNE_ACTION_VOLT_DOWN;
            at->state = AUTOTUNE_STATE_WARMING;
            apply_step(GLOBAL_STATE, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
        } else {
            // Highest trusted frequency, lowest voltage that's still stable
            // at it -- this is the best known efficient point. Hold here.
            at->state = (at->extended_freq_mhz > 0.0f) ? AUTOTUNE_STATE_BEYOND_SPEC : AUTOTUNE_STATE_AT_CEILING;
            float applied_freq = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : (float) freq_options[at->freq_step_index];
            ESP_LOGI(TAG, "At best known efficient point for %s profile (%.0f MHz @ %.0f mV) and stable, holding",
                     profile->name, applied_freq, at->voltage_mv);
        }

        // Continuous mode: re-open a fresh window immediately so we keep
        // re-checking (and can climb back up if e.g. ambient temp drops, or
        // step down again if it rises) for as long as autotune is enabled.
        window_open = false;
    }
}
