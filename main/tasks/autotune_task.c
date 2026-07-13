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

// --- Tuning behaviour: two selectable profiles (NVS_CONFIG_AUTOTUNE_PROFILE) ---
//
// Everything that stays the same across profiles (timing, hard safety
// ceilings) is still a plain #define below. What's actually being
// optimized for -- efficiency (peak hash/watt) or performance (top speed)
// -- lives in this table instead. Whether Performance is also allowed to
// climb past the vendor-tested table is a separate axis entirely: it only
// happens when Custom Settings is *also* unlocked (NVS_CONFIG_OVERCLOCK_ENABLED)
// -- see where allow_beyond_spec is used.
typedef struct {
    const char * name;
    float target_temp_c;              // stop climbing once the chip reaches this temperature
    float error_percentage_max;       // vendor-table ASIC-error tolerance
    float extended_error_percentage_max; // beyond-spec ASIC-error tolerance
    bool allow_beyond_spec;           // ever climb past the vendor table, even if unlocked
    bool allow_voltage_rescue;        // try extra voltage to save a failing frequency step
    bool optimize_efficiency;         // stop climbing at peak hash/watt instead of at the thermal/stability ceiling
} AutotuneProfileConfig;

static const AutotuneProfileConfig AUTOTUNE_PROFILES[] = {
    [AUTOTUNE_PROFILE_EFFICIENCY] = {
        .name = "efficiency",
        .target_temp_c = 65.0f,        // a safety-net ceiling -- efficiency-seeking should stop well before this anyway
        .error_percentage_max = 1.0f,
        .extended_error_percentage_max = 0.5f,
        .allow_beyond_spec = false,    // never, regardless of Custom Settings -- contradicts the whole point of this profile
        .allow_voltage_rescue = false, // accept a lower ceiling rather than spend extra heat/power to hold one
        .optimize_efficiency = true,   // stops at peak hash/watt -- see efficiency_still_improving()
    },
    [AUTOTUNE_PROFILE_PERFORMANCE] = {
        .name = "performance",
        .target_temp_c = 73.0f,
        .error_percentage_max = 2.0f,
        .extended_error_percentage_max = 1.0f,
        .allow_beyond_spec = true,     // only actually happens if NVS_CONFIG_OVERCLOCK_ENABLED is also set
        .allow_voltage_rescue = true,
        .optimize_efficiency = false,
    },
};
#define AUTOTUNE_PROFILE_COUNT (sizeof(AUTOTUNE_PROFILES) / sizeof(AUTOTUNE_PROFILES[0]))

static const AutotuneProfileConfig * get_active_profile(void)
{
    uint16_t idx = nvs_config_get_u16(NVS_CONFIG_AUTOTUNE_PROFILE);
    if (idx >= AUTOTUNE_PROFILE_COUNT) {
        idx = AUTOTUNE_PROFILE_PERFORMANCE; // unexpected NVS value -- fall back to the safe default
    }
    return &AUTOTUNE_PROFILES[idx];
}

// How often each step gets re-checked. Every signal below (temperature,
// input voltage, ASIC error rate, hashrate) is evaluated fresh on every
// tick rather than accumulated over a longer window -- there's no more
// share-counting involved at all (see the removal of the old reject-rate
// check further down for why).
#define TICK_MS 10000

// After applying any step, the chip needs a brief moment to actually
// settle (PLL relock, voltage regulator ramp) before its temperature/error
// readings mean anything. Without this, a normal settling transient can
// get misread as genuine instability -- most damagingly on the very first
// step after a fresh (re-)enable, which is also usually the single biggest
// frequency/voltage jump of the whole session (whatever it was before,
// straight down to the bottom of the table) and has nowhere lower left to
// retreat to if that transient trips a check.
#define SETTLING_GRACE_MS (10 * 1000)

// How many consecutive clean 10-second checks (all signals fine) are
// required before a step is treated as confirmed stable and the tuner
// decides what to do next. The bottom of the vendor-tested table is, almost
// by definition, the safest region to be in -- it's well within what the
// manufacturer validated -- so it only needs a couple of clean checks.
// Steps closer to the vendor's own ceiling get more scrutiny, and anything
// beyond-spec (unvalidated territory) gets the most.
#define GOOD_CHECKS_REQUIRED_FAST 2
#define GOOD_CHECKS_REQUIRED_NORMAL 4
#define GOOD_CHECKS_REQUIRED_EXTENDED 6

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

// The board's *input* voltage (from the power supply/USB-C, not the ASIC
// core voltage) can sag under load well before the ASIC itself shows any
// sign of trouble -- if the power brick can't sustain the draw, that's a
// power-delivery problem, not an ASIC stability problem, and it can
// precede hashrate/error-register symptoms entirely. Checked against a
// fraction of this board's own nominal input voltage (5V or 12V,
// depending on the model) rather than a fixed number, since that varies
// by board family. Never offer a voltage rescue for this -- pushing the
// ASIC's own voltage higher would only draw more current from a supply
// that's already struggling. Reacts immediately (no debounce), same as
// temperature -- both are direct physical readings, not noisy ratios.
#define INPUT_VOLTAGE_SAG_FRACTION 0.92f

// Minimum acceptable ratio of measured to theoretically-expected hashrate
// for the current frequency (see expected_hashrate() in
// power_management_task.c). Deliberately lenient -- some natural variance
// between measured and theoretical hashrate is normal -- but low enough to
// catch a meaningful chunk of the ASIC going quiet, which insufficient
// voltage can cause without tripping the error-rate check at all.
#define HASHRATE_RATIO_MIN 0.80f

// How many consecutive checks a signal has to stay bad before it's
// trusted. current_hashrate and error_percentage are both short-interval
// instantaneous readings (see hashrate_monitor_task.c) with real
// sample-to-sample variance, especially at low absolute hashrates -- a
// single bad reading isn't enough on its own.
#define HASHRATE_SHORTFALL_TICKS_REQUIRED 2
#define ASIC_ERROR_BAD_TICKS_REQUIRED 2

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
// instability well before it fails as damage, which is why only frequency
// is allowed to explore past vendor data here.
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

// After this many failures near roughly the same beyond-spec frequency
// (not necessarily consecutive -- see track_extended_climb_failure), treat
// that point as off-limits for a cooldown period rather than retrying it
// forever, letting the tuner move on to voltage optimization in the meantime.
#define EXTENDED_FAIL_LIMIT 2

// How long a frequency stays off-limits after repeatedly failing near it,
// before the tuner is willing to probe it again. An hour is long enough for
// ambient conditions (or whatever else was marginal) to plausibly have
// changed, without abandoning a genuinely-too-high frequency for the rest
// of an indefinitely-long session.
#define EXTENDED_COOLDOWN_MS (60LL * 60LL * 1000LL)

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

// True while still in the bottom half of the vendor-tested table. Used to
// pick a lower GOOD_CHECKS_REQUIRED (this is the safest, most thoroughly
// vendor-tested part of the range) and to gate the proactive voltage climb
// below (which otherwise reached vendor-max voltage well before frequency
// did, spending more heat/power than most of that climb ever actually needed).
static bool in_fast_climb_zone(AutotuneModule * at, int freq_option_count)
{
    return at->extended_freq_mhz <= 0.0f && at->freq_step_index < (freq_option_count / 2);
}

// Scales how far we are into the frequency table onto the voltage range, so
// voltage can be raised proactively in step with frequency instead of only
// reactively once a step actually fails. This doesn't compromise final
// efficiency: the undervolt-optimization phase that runs once the ceiling
// is reached will still shave voltage back down, in the same fine 10mV
// steps, to whatever the minimum actually needed turns out to be.
static float proportional_voltage_mv(int freq_step_index, int freq_option_count, float min_voltage_mv, float max_voltage_mv)
{
    if (freq_option_count <= 1) {
        return min_voltage_mv;
    }
    float progress = (float) freq_step_index / (float) (freq_option_count - 1);
    return min_voltage_mv + progress * (max_voltage_mv - min_voltage_mv);
}

// For efficiency-optimizing profiles (currently just Eco), whether climbing
// one more frequency step is still worth it in hash/watt terms. Voltage
// requirements tend to grow faster than frequency near the top of a chip's
// range, so peak efficiency is usually reached well before the thermal or
// stability ceiling. Records the current step as the new best point as a
// side effect whenever it does still look like an improvement. Returns
// true (i.e. "keep climbing") if there's no usable power reading yet,
// rather than blocking progress on missing data.
static bool efficiency_still_improving(AutotuneModule * at, SystemModule * sys_module, PowerManagementModule * pm)
{
    if (pm->power < 0.5f) {
        return true;
    }
    float current_efficiency = sys_module->current_hashrate / pm->power; // GH/s per W
    if (at->best_efficiency_freq_step < 0 || current_efficiency >= at->best_efficiency_ghs_per_watt) {
        at->best_efficiency_ghs_per_watt = current_efficiency;
        at->best_efficiency_freq_step = at->freq_step_index;
        return true;
    }
    return false;
}

static void apply_step(GlobalState * GLOBAL_STATE, AutotuneModule * at, const uint16_t * freq_options,
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
    at->step_applied_at_ms = esp_timer_get_time() / 1000;
    // Every applied step -- whether a climb or a revert -- starts its own
    // fresh confirmation period. Debounce counters reset here too, since
    // they track whether *this* step looks bad, not some previous one.
    at->consecutive_good_checks = 0;
    at->asic_error_bad_ticks = 0;
    at->hashrate_shortfall_ticks = 0;
}

static void resync_from_nvs(AutotuneModule * at, const uint16_t * freq_options, int freq_option_count)
{
    float current_freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    at->freq_step_index = find_index(freq_options, (uint16_t) current_freq);
    at->voltage_mv = (float) nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);

    float vendor_max_freq = (float) freq_options[freq_option_count - 1];
    at->extended_freq_mhz = (current_freq > vendor_max_freq + 0.5f) ? current_freq : 0.0f;
    at->extended_soft_ceiling_mhz = 0.0f;
    at->extended_soft_ceiling_expiry_ms = 0;
    at->extended_last_failed_mhz = 0.0f;
    at->extended_freq_consecutive_fails = 0;
    at->best_efficiency_ghs_per_watt = 0.0f;
    at->best_efficiency_freq_step = -1;
    at->hashrate_shortfall_ticks = 0;
    at->asic_error_bad_ticks = 0;
    at->consecutive_good_checks = 0;
    at->last_action = AUTOTUNE_ACTION_NONE;
}

// Undo whatever the last applied step was, because it turned out unstable.
// Reverting the *specific* thing that changed -- rather than always
// assuming "lower the frequency" -- is what makes the undervolt trials and
// the extra-voltage-for-stability trials safe to attempt at all.
static void revert_last_action(AutotuneModule * at, const uint16_t * freq_options, int freq_option_count,
                                float min_voltage_mv, float max_voltage_mv, bool allow_voltage_rescue)
{
    // A frequency step that failed on something other than heat sometimes
    // just needs a bit more voltage headroom rather than being abandoned
    // outright. Only offer that rescue when the caller says it's
    // appropriate -- never for a temperature-driven revert, since adding
    // voltage there would work against the very problem we're reacting to.
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
    float actual_volt = nvs_config_get_float(NVS_CONFIG_ASIC_VOLTAGE);

    bool freq_matches = fabsf(actual_freq - expected_freq) < 0.5f;
    // Voltage is stored as an integer mV in NVS, so allow for that rounding
    // when comparing against our own float-tracked value.
    bool volt_matches = fabsf(actual_volt - at->voltage_mv) < 1.0f;

    return !freq_matches || !volt_matches;
}

// Called on every reverted/backed-off step. If the step we just gave up on
// was a beyond-spec frequency climb, count it -- after enough failures near
// roughly the same point, get_extended_ceiling_mhz() will stop letting the
// tuner retry it (see extended_soft_ceiling_mhz) and it'll move on to
// voltage optimization instead. Any other kind of revert (an undervolt
// trial, or a vendor-table-only step) resets the streak, since it's not
// part of a beyond-spec climb attempt.
static void track_extended_climb_failure(AutotuneModule * at, const char * reason)
{
    if (at->extended_freq_mhz <= 0.0f) {
        at->extended_freq_consecutive_fails = 0;
        return;
    }

    bool near_last_failure = fabsf(at->extended_freq_mhz - at->extended_last_failed_mhz) < (EXTENDED_STEP_MHZ * 1.5f);
    at->extended_freq_consecutive_fails = near_last_failure ? at->extended_freq_consecutive_fails + 1 : 1;
    at->extended_last_failed_mhz = at->extended_freq_mhz;

    if (at->extended_freq_consecutive_fails >= EXTENDED_FAIL_LIMIT) {
        // extended_freq_mhz still holds the failing value here; the last
        // known-good level is one step below it.
        at->extended_soft_ceiling_mhz = at->extended_freq_mhz - EXTENDED_STEP_MHZ;
        at->extended_soft_ceiling_expiry_ms = (esp_timer_get_time() / 1000) + EXTENDED_COOLDOWN_MS;
        ESP_LOGI(TAG, "Giving up climbing past %.0f MHz after %d failures near this level (%s), backing off for an hour",
                 at->extended_soft_ceiling_mhz, at->extended_freq_consecutive_fails, reason);
    }
}

// A newly-set (or still-active) soft ceiling only blocks *future* climb
// attempts on its own -- it doesn't retroactively touch whatever frequency
// the tuner happens to be sitting at right now. Without this, a voltage
// rescue can keep holding the tuner at the exact frequency that just
// triggered the ceiling in the first place, instead of actually retreating
// to the last confirmed-good level. Call this right after
// revert_last_action(), before applying the step, so the ceiling is
// honored immediately rather than only being enforced the next time a
// climb is attempted.
static void enforce_soft_ceiling(AutotuneModule * at)
{
    if (at->extended_soft_ceiling_mhz > 0.0f && at->extended_freq_mhz > at->extended_soft_ceiling_mhz) {
        at->extended_freq_mhz = at->extended_soft_ceiling_mhz;
        at->last_action = AUTOTUNE_ACTION_NONE;
    }
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
            was_enabled = false;
            continue;
        }

        // Re-read every tick rather than caching at startup: the person can
        // switch profiles while autotune is running, and it should take
        // effect on the next evaluation rather than requiring a re-enable.
        const AutotuneProfileConfig * profile = get_active_profile();
        at->active_profile = (AutotuneProfile) (profile - AUTOTUNE_PROFILES);
        float temp_ceiling_c = profile->target_temp_c;

        // A frequency that repeatedly failed gets a cooldown rather than a
        // permanent ban for the rest of the session -- once that cooldown
        // has passed, forget it ever failed and let the tuner probe it
        // again (ambient conditions, or whatever else was marginal, may
        // well have changed by now).
        if (at->extended_soft_ceiling_mhz > 0.0f &&
            (esp_timer_get_time() / 1000) > at->extended_soft_ceiling_expiry_ms) {
            ESP_LOGI(TAG, "Cooldown expired for the %.0f MHz soft ceiling, willing to probe higher again",
                     at->extended_soft_ceiling_mhz);
            at->extended_soft_ceiling_mhz = 0.0f;
            at->extended_freq_consecutive_fails = 0;
        }

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
            at->extended_soft_ceiling_expiry_ms = 0;
            at->extended_last_failed_mhz = 0.0f;
            at->extended_freq_consecutive_fails = 0;
            at->best_efficiency_ghs_per_watt = 0.0f;
            at->best_efficiency_freq_step = -1;
            at->last_action = AUTOTUNE_ACTION_NONE;
            apply_step(GLOBAL_STATE, at, freq_options, 0, at->voltage_mv, 0.0f);
            ESP_LOGI(TAG, "Autotune enabled (%s profile): starting from the bottom (%.0f MHz @ %.0f mV) and climbing",
                     profile->name, (float) freq_options[0], at->voltage_mv);
            was_enabled = true;
            continue;
        }

        bool blocked = sys_module->overheat_mode || sys_module->mining_paused ||
                        sys_module->hardware_fault || sys_module->pools_unavailable ||
                        !GLOBAL_STATE->ASIC_initalized;

        if (blocked) {
            // Something else (most likely the reactive overheat-protection in
            // power_management_task, or the ASIC simply not being ready yet
            // right after boot) has taken over, or we're not ready to act
            // yet at all. Crucially, this is checked *before* the
            // just-enabled reset above: writing a frequency/voltage step
            // while the ASIC hasn't finished its own init sequence could
            // interfere with it. Don't fight it: just resync our own idea
            // of the current step to whatever is actually configured now
            // (a no-op if nothing's been applied yet), and wait until
            // things are calm/ready.
            at->state = AUTOTUNE_STATE_PAUSED;
            resync_from_nvs(at, freq_options, freq_option_count);
            continue;
        }

        if (settings_changed_externally(at, freq_options)) {
            // The person (or something else) changed frequency/voltage
            // directly, outside the tuner. Treat that as a deliberate new
            // starting point rather than silently overwriting it on the next
            // action -- resync to it and start a fresh confirmation period.
            ESP_LOGI(TAG, "Frequency/voltage changed externally, adopting new starting point");
            resync_from_nvs(at, freq_options, freq_option_count);
            continue;
        }

        // Give the chip a moment to settle after any step before judging
        // it -- see the SETTLING_GRACE_MS comment for why.
        if ((esp_timer_get_time() / 1000) - at->step_applied_at_ms < SETTLING_GRACE_MS) {
            continue;
        }

        float chip_temp = pm->chip_temp_avg;
        if (pm->chip_temp2_avg > chip_temp) {
            chip_temp = pm->chip_temp2_avg;
        }
        at->max_temp_seen_this_window = chip_temp;

        // 1. Temperature -- reacts immediately, no debounce, no voltage
        // rescue (that would only make a heat problem worse).
        if (chip_temp > temp_ceiling_c) {
            ESP_LOGW(TAG, "Temp %.1fC over ceiling %.1fC, stepping down early",
                     chip_temp, temp_ceiling_c);
            track_extended_climb_failure(at, "heat");
            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, false);
            enforce_soft_ceiling(at);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            continue;
        }

        // 2. Input voltage sag -- also immediate, also never rescued with
        // more ASIC voltage (see INPUT_VOLTAGE_SAG_FRACTION comment).
        float nominal_input_voltage = (float) GLOBAL_STATE->DEVICE_CONFIG.family.nominal_voltage;
        float input_voltage_floor = nominal_input_voltage * INPUT_VOLTAGE_SAG_FRACTION;
        if (pm->voltage > 0.5f && pm->voltage < input_voltage_floor) {
            ESP_LOGW(TAG, "Input voltage %.2fV under %.0f%% of nominal %.0fV, stepping down early (power supply may be struggling)",
                     pm->voltage, INPUT_VOLTAGE_SAG_FRACTION * 100.0f, nominal_input_voltage);
            track_extended_climb_failure(at, "input voltage sag");
            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, false);
            enforce_soft_ceiling(at);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            continue;
        }

        // 3. ASIC on-chip error rate -- debounced (see
        // ASIC_ERROR_BAD_TICKS_REQUIRED), voltage rescue offered first if
        // this profile allows it, since insufficient voltage headroom is a
        // common cause.
        float error_threshold = at->extended_freq_mhz > 0.0f ? profile->extended_error_percentage_max : profile->error_percentage_max;
        if (sys_module->error_percentage > error_threshold) {
            at->asic_error_bad_ticks++;
        } else {
            at->asic_error_bad_ticks = 0;
        }
        if (at->asic_error_bad_ticks >= ASIC_ERROR_BAD_TICKS_REQUIRED) {
            ESP_LOGW(TAG, "ASIC error rate %.1f%% over threshold %.1f%%, sustained over %d checks, reverting",
                     sys_module->error_percentage, error_threshold, at->asic_error_bad_ticks);
            at->asic_error_bad_ticks = 0;
            track_extended_climb_failure(at, "ASIC errors");
            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, profile->allow_voltage_rescue);
            enforce_soft_ceiling(at);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            continue;
        }

        // 4. Hashrate shortfall against the chip's own theoretical
        // expectation for this frequency -- also debounced. Insufficient
        // voltage can cause some cores to simply stop contributing rather
        // than produce wrong results, which neither of the checks above
        // would catch on their own.
        if (pm->expected_hashrate > 0.5f && sys_module->current_hashrate < pm->expected_hashrate * HASHRATE_RATIO_MIN) {
            at->hashrate_shortfall_ticks++;
        } else {
            at->hashrate_shortfall_ticks = 0;
        }
        if (at->hashrate_shortfall_ticks >= HASHRATE_SHORTFALL_TICKS_REQUIRED) {
            ESP_LOGW(TAG, "Hashrate %.1f GH/s is only %.0f%% of the %.1f GH/s expected for this frequency, sustained over %d checks, reverting (possible partial core dropout from insufficient voltage)",
                     sys_module->current_hashrate, (sys_module->current_hashrate / pm->expected_hashrate) * 100.0f,
                     pm->expected_hashrate, at->hashrate_shortfall_ticks);
            at->hashrate_shortfall_ticks = 0;
            track_extended_climb_failure(at, "hashrate shortfall");
            revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, profile->allow_voltage_rescue);
            enforce_soft_ceiling(at);
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            continue;
        }

        // All four signals look fine on this check.
        at->state = AUTOTUNE_STATE_WARMING;
        at->consecutive_good_checks++;

        int good_checks_required = GOOD_CHECKS_REQUIRED_NORMAL;
        if (at->extended_freq_mhz > 0.0f) {
            good_checks_required = GOOD_CHECKS_REQUIRED_EXTENDED;
        } else if (in_fast_climb_zone(at, freq_option_count)) {
            good_checks_required = GOOD_CHECKS_REQUIRED_FAST;
        }

        if (at->consecutive_good_checks < good_checks_required) {
            continue; // not yet confirmed stable -- keep checking
        }
        at->consecutive_good_checks = 0;

        if (at->last_action == AUTOTUNE_ACTION_VOLT_UP) {
            // The extra voltage successfully stabilized this frequency step.
            // Keep it, and go straight back to trying to climb frequency
            // further on the next cycle. This frequency level did ultimately
            // work, so it shouldn't count toward "repeatedly failing here".
            at->extended_freq_consecutive_fails = 0;
            at->last_action = AUTOTUNE_ACTION_NONE;
        } else if (at->freq_step_index < freq_option_count - 1 &&
                   (!profile->optimize_efficiency || efficiency_still_improving(at, sys_module, pm))) {
            at->freq_step_index++;
            at->last_action = AUTOTUNE_ACTION_FREQ_UP;
            at->step_ups_total++;

            // Proactively bring voltage along for the ride, but only once
            // past the safe bottom half of the table -- proactively scaling
            // voltage all the way from the very first step meant it was
            // already sitting at vendor-max by the time frequency reached
            // vendor-max too, even though most of that climb never actually
            // needed it. Below that point, voltage only moves reactively
            // (see revert_last_action's rescue), the same as Eco always does.
            if (profile->allow_voltage_rescue && !in_fast_climb_zone(at, freq_option_count)) {
                float target_voltage = proportional_voltage_mv(at->freq_step_index, freq_option_count, min_voltage_mv, max_voltage_mv);
                if (target_voltage > at->voltage_mv) {
                    at->voltage_mv = clamp_voltage(target_voltage, min_voltage_mv, max_voltage_mv);
                }
            }

            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, 0.0f);
        } else if (profile->optimize_efficiency && at->best_efficiency_freq_step >= 0 &&
                   at->freq_step_index > at->best_efficiency_freq_step) {
            // Efficiency dropped compared to the best point seen -- we've
            // passed the peak. Retreat one step at a time back toward it
            // rather than continuing to climb; once there, this falls
            // through to the normal undervolt-optimization phase below,
            // same as any other profile that's done climbing.
            at->freq_step_index--;
            at->last_action = AUTOTUNE_ACTION_NONE;
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, 0.0f);
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
            // Only treat this as "real progress" (and forget past failures)
            // if it's not just re-attempting a level we already know is
            // troublesome.
            bool near_known_failure = fabsf(at->extended_freq_mhz - at->extended_last_failed_mhz) < (EXTENDED_STEP_MHZ * 1.5f);
            if (!near_known_failure) {
                at->extended_freq_consecutive_fails = 0;
            }
            at->last_action = AUTOTUNE_ACTION_FREQ_UP;
            at->step_ups_total++;
            at->state = AUTOTUNE_STATE_BEYOND_SPEC;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
        } else if (at->voltage_mv > min_voltage_mv + 0.5f) {
            // The highest frequency we currently trust is maxed out (vendor
            // ceiling, or the beyond-spec ceiling if that's active) and
            // stable. Try shaving voltage down at this same frequency, in
            // fine 10mV steps -- same hashrate, less heat, less power, if
            // it holds.
            at->voltage_mv = clamp_voltage(at->voltage_mv - VOLTAGE_STEP_MV, min_voltage_mv, max_voltage_mv);
            at->last_action = AUTOTUNE_ACTION_VOLT_DOWN;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
        } else {
            // Highest trusted frequency, lowest voltage that's still stable
            // at it -- this is the best known efficient point. Hold here.
            at->state = (at->extended_freq_mhz > 0.0f) ? AUTOTUNE_STATE_BEYOND_SPEC : AUTOTUNE_STATE_AT_CEILING;
            float applied_freq = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : (float) freq_options[at->freq_step_index];
            ESP_LOGI(TAG, "At best known efficient point for %s profile (%.0f MHz @ %.0f mV) and stable, holding",
                     profile->name, applied_freq, at->voltage_mv);
        }
    }
}