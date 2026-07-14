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
// beyond-spec (unvalidated territory) gets the most. GOOD_CHECKS_REQUIRED_FAST
// specifically is only used for profiles chasing speed (Performance) --
// Eco has no reason to rush through this region and uses the normal
// confirmation time there instead (see where good_checks_required is set).
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
//
// The step size itself is bigger while still within the vendor-tested
// table (converges to a working voltage in fewer rescue/undervolt cycles,
// each of which costs a full debounce round-trip) and finer once exploring
// beyond-spec, where the extra precision matters more than the extra speed
// in unvalidated territory.
#define VOLTAGE_STEP_MV_NORMAL 25.0f
#define VOLTAGE_STEP_MV_EXTENDED 10.0f

static float get_voltage_step_mv(const AutotuneModule * at)
{
    return at->extended_freq_mhz > 0.0f ? VOLTAGE_STEP_MV_EXTENDED : VOLTAGE_STEP_MV_NORMAL;
}

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

// How long Eco (optimize_efficiency) holds firmly at a found peak before
// checking whether a higher peak is now reachable -- same duration as
// EXTENDED_COOLDOWN_MS, for the same reason: long enough for ambient
// conditions to plausibly have changed, without constantly re-probing.
#define EFFICIENCY_SETTLE_MS (60LL * 60LL * 1000LL)

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

// For efficiency-optimizing profiles (currently just Eco), whether climbing
// one more frequency step is still worth it in hash/watt terms. Voltage
// requirements tend to grow faster than frequency near the top of a chip's
// range, so peak efficiency is usually reached well before the thermal or
// stability ceiling. Records the current step as the new best point as a
// side effect whenever it does still look like an improvement. Returns
// true (i.e. "keep climbing") if there's no usable power reading yet,
// rather than blocking progress on missing data.
// For ASIC chips, power draw scales roughly with voltage-squared times
// frequency, while hashrate scales roughly linearly with frequency alone --
// so hash/watt efficiency is mostly driven by voltage, not frequency. Since
// the vendor table already pairs each frequency step with a reasonably
// well-matched voltage, the efficiency curve climbing through it tends to
// stay fairly flat for a long stretch rather than clearly peaking well
// below the top. Requiring the *literal* peak (any improvement at all, no
// matter how tiny, counts as "still worth climbing for") means Eco often
// ends up barely different from Performance. Requiring a genuinely
// meaningful improvement instead makes it stop once returns diminish, for
// a result that's actually noticeably more efficient, not just technically
// optimal by a fraction of a percent.
#define EFFICIENCY_MIN_IMPROVEMENT_RATIO 1.02f

static bool efficiency_still_improving(AutotuneModule * at, SystemModule * sys_module, PowerManagementModule * pm,
                                        int freq_option_count)
{
    if (pm->power < 0.5f) {
        return true;
    }
    float current_efficiency = sys_module->current_hashrate / pm->power; // GH/s per W

    if (in_fast_climb_zone(at, freq_option_count)) {
        // Within the fast-climb zone, voltage never moves (the proactive
        // voltage climb further down is itself gated to skip this same
        // zone), so hash/watt differences between adjacent frequency steps
        // there are naturally tiny -- efficiency is mostly a function of
        // voltage, not frequency, so climbing frequency alone at a fixed
        // voltage barely moves it either way. This needs to be a genuinely
        // unconditional "keep climbing", not just a very low bar: even a
        // bar of "at least as good as before, no threshold" can still fail
        // on nothing more than ordinary measurement noise, which would
        // cause the exact same "give up and settle at the very bottom"
        // problem this is meant to avoid. Still record the running best
        // (without letting a worse reading overwrite a better one) so the
        // comparison baseline is accurate by the time real trade-offs
        // start to matter just past this zone.
        if (at->best_efficiency_freq_step < 0 || current_efficiency > at->best_efficiency_ghs_per_watt) {
            at->best_efficiency_ghs_per_watt = current_efficiency;
        }
        at->best_efficiency_freq_step = at->freq_step_index;
        return true;
    }

    if (at->best_efficiency_freq_step < 0 || current_efficiency >= at->best_efficiency_ghs_per_watt * EFFICIENCY_MIN_IMPROVEMENT_RATIO) {
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
    at->last_confirmed_extended_freq_mhz = at->extended_freq_mhz;
    at->last_confirmed_voltage_mv = at->voltage_mv;
    at->best_efficiency_ghs_per_watt = 0.0f;
    at->best_efficiency_freq_step = -1;
    at->efficiency_settled_expiry_ms = 0;
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
        at->voltage_mv = clamp_voltage(at->voltage_mv + get_voltage_step_mv(at), min_voltage_mv, max_voltage_mv);
        at->last_action = AUTOTUNE_ACTION_VOLT_UP;
        return;
    }

    switch (at->last_action) {
        case AUTOTUNE_ACTION_VOLT_DOWN:
            // The undervolt trial didn't hold -- go back up to the last
            // known-good voltage for this frequency.
            at->voltage_mv = clamp_voltage(at->voltage_mv + get_voltage_step_mv(at), min_voltage_mv, max_voltage_mv);
            break;
        case AUTOTUNE_ACTION_VOLT_UP:
            // Even extra voltage didn't stabilize this frequency -- undo the
            // voltage bump AND give up on this frequency step. If we were
            // exploring beyond-spec, that "step" lives in extended_freq_mhz,
            // not freq_step_index (which stays pinned at the vendor max
            // throughout beyond-spec exploration) -- back off the right one.
            at->voltage_mv = clamp_voltage(at->voltage_mv - get_voltage_step_mv(at), min_voltage_mv, max_voltage_mv);
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
                at->voltage_mv = clamp_voltage(at->voltage_mv - get_voltage_step_mv(at), min_voltage_mv, max_voltage_mv);
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
// was a beyond-spec frequency climb, count it -- after enough failures near
// roughly the same point, this stops exploring entirely for a while and
// falls back to the last frequency/voltage combo that actually proved
// itself, rather than a calculated "a bit lower" guess. Returns true if it
// just did that full restoration itself, in which case the caller should
// skip revert_last_action()/enforce_soft_ceiling() and go straight to
// apply_step() -- those would otherwise fight over what to do with a state
// this function already fully resolved.
static bool track_extended_climb_failure(AutotuneModule * at, const uint16_t * freq_options, int freq_option_count,
                                          const char * reason)
{
    // Only a failure that happened while actually trying to reach or hold
    // this frequency (a straight climb, or a voltage rescue attempting to
    // save one) says anything about whether the frequency itself is too
    // high. A failure during an undervolt trial (AUTOTUNE_ACTION_VOLT_DOWN)
    // just means voltage was shaved one step too far while searching for
    // the minimum at an already-working frequency -- that's the expected,
    // normal way that search ends, not evidence the frequency is bad, and
    // revert_last_action's own VOLT_DOWN case already walks it back
    // correctly on its own without needing any help here.
    bool was_climb_related = at->last_action == AUTOTUNE_ACTION_FREQ_UP || at->last_action == AUTOTUNE_ACTION_VOLT_UP;
    if (!was_climb_related || at->extended_freq_mhz <= 0.0f) {
        at->extended_freq_consecutive_fails = 0;
        return false;
    }

    bool near_last_failure = fabsf(at->extended_freq_mhz - at->extended_last_failed_mhz) < (EXTENDED_STEP_MHZ * 1.5f);
    at->extended_freq_consecutive_fails = near_last_failure ? at->extended_freq_consecutive_fails + 1 : 1;
    at->extended_last_failed_mhz = at->extended_freq_mhz;

    if (at->extended_freq_consecutive_fails < EXTENDED_FAIL_LIMIT) {
        return false;
    }

    // Give up entirely: stop exploring (no more climbing, no more
    // voltage shaving) and hold the last confirmed-good combo fixed for
    // the cooldown period. Reactive safety checks (temperature, input
    // voltage, and the debounced ASIC-error/hashrate checks) still apply
    // throughout regardless -- this only pauses further exploration.
    float vendor_max_freq = (float) freq_options[freq_option_count - 1];
    at->extended_freq_mhz = at->last_confirmed_extended_freq_mhz; // 0 if nothing beyond vendor spec was ever confirmed
    at->voltage_mv = at->last_confirmed_voltage_mv;
    at->extended_soft_ceiling_mhz = at->last_confirmed_extended_freq_mhz > 0.0f ? at->last_confirmed_extended_freq_mhz : vendor_max_freq;
    at->extended_soft_ceiling_expiry_ms = (esp_timer_get_time() / 1000) + EXTENDED_COOLDOWN_MS;
    at->last_action = AUTOTUNE_ACTION_NONE;
    ESP_LOGI(TAG, "Giving up after %d failures near %.0f MHz (%s) -- holding the last confirmed-good %.0f MHz @ %.0f mV for an hour",
             at->extended_freq_consecutive_fails, at->extended_last_failed_mhz, reason,
             at->extended_freq_mhz, at->voltage_mv);
    return true;
}

// A newly-set (or still-active) soft ceiling only blocks *future* climb
// attempts on its own -- it doesn't retroactively touch whatever frequency
// the tuner happens to be sitting at right now. Without this, a voltage
// rescue that's already been confirmed stable could keep holding the tuner
// at a frequency above the ceiling instead of actually retreating to the
// last confirmed-good level. Call this right after revert_last_action(),
// before applying the step, so the ceiling is honored immediately rather
// than only being enforced the next time a climb is attempted -- except
// for a rescue that was *just* attempted this same cycle (see the
// AUTOTUNE_ACTION_VOLT_UP check below), which needs a real chance to be
// tested before anything decides it didn't work.
static void enforce_soft_ceiling(AutotuneModule * at)
{
    if (at->last_action == AUTOTUNE_ACTION_VOLT_UP) {
        // A voltage rescue was just attempted for the frequency that
        // triggered this ceiling -- it hasn't been given a chance to prove
        // itself yet. Dragging frequency down here regardless would mean
        // the rescue mechanism could never actually be tested: every
        // failure would immediately force a step down on the same cycle,
        // before revert_last_action's own VOLT_UP-failure handling (which
        // already backs off correctly if the rescue itself later fails)
        // ever gets a chance to run.
        return;
    }
    if (at->extended_soft_ceiling_mhz > 0.0f && at->extended_freq_mhz > at->extended_soft_ceiling_mhz) {
        at->extended_freq_mhz = at->extended_soft_ceiling_mhz;
        at->last_action = AUTOTUNE_ACTION_NONE;
    }
}

// Resets to the bottom of the vendor table and lets the normal climb take
// it from there -- frequency climbs step by step, and voltage only goes up
// when a frequency step actually needs the rescue to stabilize (see
// revert_last_action), or proactively in step with frequency, one
// get_voltage_step_mv increment at a time once past the fast-climb zone.
// This tends toward a near-minimal voltage for whatever frequency is
// reached, rather than carrying over whatever frequency/voltage happened
// to be set before (which could be a world away from what's actually
// efficient/safe under a newly-selected profile's own rules) and only ever
// shaving it down from there.
static void reset_to_bottom_and_climb(GlobalState * GLOBAL_STATE, AutotuneModule * at, const uint16_t * freq_options,
                                       float min_voltage_mv, const AutotuneProfileConfig * profile, const char * reason)
{
    at->freq_step_index = 0;
    at->voltage_mv = min_voltage_mv;
    at->extended_freq_mhz = 0.0f;
    at->extended_soft_ceiling_mhz = 0.0f;
    at->extended_soft_ceiling_expiry_ms = 0;
    at->extended_last_failed_mhz = 0.0f;
    at->extended_freq_consecutive_fails = 0;
    at->last_confirmed_extended_freq_mhz = 0.0f;
    at->last_confirmed_voltage_mv = min_voltage_mv;
    at->best_efficiency_ghs_per_watt = 0.0f;
    at->best_efficiency_freq_step = -1;
    at->efficiency_settled_expiry_ms = 0;
    at->last_action = AUTOTUNE_ACTION_NONE;
    apply_step(GLOBAL_STATE, at, freq_options, 0, at->voltage_mv, 0.0f);
    ESP_LOGI(TAG, "%s (%s profile): starting from the bottom (%.0f MHz @ %.0f mV) and climbing",
             reason, profile->name, (float) freq_options[0], at->voltage_mv);
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

    // Voltage is tuned continuously (see get_voltage_step_mv) between the
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
    // Tracks the previously-seen profile so a switch (Performance -> Eco or
    // back) can also trigger a fresh climb -- otherwise the new profile
    // would just inherit whatever frequency/voltage the old one had
    // settled on (e.g. a beyond-spec Performance point), which usually
    // isn't a sensible starting point for the new profile's own rules.
    int last_seen_profile_idx = -1;

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
            last_seen_profile_idx = -1;
            continue;
        }

        // Re-read every tick rather than caching at startup: the person can
        // switch profiles while autotune is running, and it should take
        // effect on the next evaluation rather than requiring a re-enable.
        const AutotuneProfileConfig * profile = get_active_profile();
        at->active_profile = (AutotuneProfile) (profile - AUTOTUNE_PROFILES);
        float temp_ceiling_c = profile->target_temp_c;

        // Eco never proactively climbs voltage (see the proactive-climb
        // gate further down), but without *any* ability to try a bit more
        // voltage when a frequency step fails, it can only ever explore
        // the single voltage level it started at (the vendor-table
        // minimum) -- meaning it could never discover that a slightly
        // higher frequency plus a modest voltage bump is actually a more
        // efficient combo than just stopping at a lower frequency and
        // never finding out. So: still no rescue in the fast-climb zone
        // (voltage differences don't meaningfully matter there anyway, see
        // efficiency_still_improving), but allow it once past that, where
        // real efficiency trade-offs start to exist.
        bool allow_voltage_rescue_now = profile->allow_voltage_rescue ||
                                         (profile->optimize_efficiency && !in_fast_climb_zone(at, freq_option_count));

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

        // Same idea for Eco's efficiency-peak settle: once the hold period
        // is up, allow checking for a higher peak again. Reset the
        // recorded best so the very next measurement can freely become
        // the new reference, rather than being unfairly compared against
        // a (possibly stale, ambient-condition-dependent) value from an
        // hour ago.
        if (at->efficiency_settled_expiry_ms > 0 &&
            (esp_timer_get_time() / 1000) > at->efficiency_settled_expiry_ms) {
            ESP_LOGI(TAG, "Efficiency settle period expired, willing to check for a higher peak again");
            at->efficiency_settled_expiry_ms = 0;
            at->best_efficiency_ghs_per_watt = 0.0f;
        }

        bool blocked = sys_module->overheat_mode || sys_module->mining_paused ||
                        sys_module->hardware_fault || sys_module->pools_unavailable ||
                        !GLOBAL_STATE->ASIC_initalized;

        if (blocked) {
            // Something else (most likely the reactive overheat-protection in
            // power_management_task, or the ASIC simply not being ready yet
            // right after boot) has taken over, or we're not ready to act
            // yet at all. Crucially, this is checked *before* the
            // just-enabled reset below: writing a frequency/voltage step
            // while the ASIC hasn't finished its own init sequence could
            // interfere with it. Don't fight it: just resync our own idea
            // of the current step to whatever is actually configured now
            // (a no-op if nothing's been applied yet), and wait until
            // things are calm/ready.
            at->state = AUTOTUNE_STATE_PAUSED;
            resync_from_nvs(at, freq_options, freq_option_count);
            continue;
        }

        if (!was_enabled) {
            // Just (re)enabled -- and the ASIC is confirmed initialized, so
            // it's now safe to apply a step.
            reset_to_bottom_and_climb(GLOBAL_STATE, at, freq_options, min_voltage_mv, profile, "Autotune enabled");
            was_enabled = true;
            last_seen_profile_idx = (int) at->active_profile;
            continue;
        }

        if (last_seen_profile_idx != (int) at->active_profile) {
            // The profile itself changed while already running (e.g.
            // Performance -> Eco). Whatever frequency/voltage the old
            // profile had settled on is not a meaningful starting point
            // for the new one's own rules -- most notably, switching to
            // Eco while sitting at a beyond-spec Performance point would
            // otherwise just shave a bit of voltage off that same high
            // frequency instead of actually searching for an efficient
            // one. Start the new profile fresh from the bottom instead.
            reset_to_bottom_and_climb(GLOBAL_STATE, at, freq_options, min_voltage_mv, profile, "Profile changed");
            last_seen_profile_idx = (int) at->active_profile;
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
            bool gave_up = track_extended_climb_failure(at, freq_options, freq_option_count, "heat");
            if (!gave_up) {
                revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, false);
                enforce_soft_ceiling(at);
            }
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
            bool gave_up = track_extended_climb_failure(at, freq_options, freq_option_count, "input voltage sag");
            if (!gave_up) {
                revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, false);
                enforce_soft_ceiling(at);
            }
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
            continue;
        }

        // 3. ASIC on-chip error rate -- debounced (see
        // ASIC_ERROR_BAD_TICKS_REQUIRED), voltage rescue offered first if
        // this profile allows it, since insufficient voltage headroom is a
        // common cause.
        //
        // Skipped entirely in the fast-climb zone: error_percentage is
        // error_hashrate divided by current_hashrate, and at the bottom of
        // the vendor table current_hashrate is small enough that this
        // reads as *sustainedly* elevated (10-18%+, not just a brief
        // blip) purely from that small denominator -- a couple of
        // consecutive debounce checks doesn't filter that out, it just
        // delays the same false trigger by one tick. This is also the
        // safest, most thoroughly vendor-tested part of the range, so
        // skipping this one specific check here is a reasonable trade;
        // temperature/input-voltage and the debounced hashrate-shortfall
        // check still apply throughout.
        if (!in_fast_climb_zone(at, freq_option_count)) {
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
                bool gave_up = track_extended_climb_failure(at, freq_options, freq_option_count, "ASIC errors");
                if (!gave_up) {
                    revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, allow_voltage_rescue_now);
                    enforce_soft_ceiling(at);
                }
                at->step_downs_total++;
                apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
                continue;
            }
        }

        // 4. Hashrate shortfall against the chip's own theoretical
        // expectation for this frequency -- also debounced. Insufficient
        // voltage can cause some cores to simply stop contributing rather
        // than produce wrong results, which neither of the checks above
        // would catch on their own.
        //
        // Skipped in the fast-climb zone for the same reason as the
        // ASIC-error check: current_hashrate is a small, short-interval
        // measurement there, with real sample-to-sample variance that
        // debouncing over a couple of ticks doesn't fully filter out if
        // it's sustained rather than a one-off blip. Without this
        // exemption, a profile that never does a voltage rescue (Eco) and
        // starts at the very bottom of the table (freq_step_index=0,
        // voltage already at the floor) has nowhere left to retreat to if
        // this fires there -- revert_last_action can't lower anything
        // further, so it would just get stuck rather than ever climbing
        // out of the fast-climb zone at all.
        if (!in_fast_climb_zone(at, freq_option_count)) {
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
                bool gave_up = track_extended_climb_failure(at, freq_options, freq_option_count, "hashrate shortfall");
                if (!gave_up) {
                    revert_last_action(at, freq_options, freq_option_count, min_voltage_mv, max_voltage_mv, allow_voltage_rescue_now);
                    enforce_soft_ceiling(at);
                }
                at->step_downs_total++;
                apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
                continue;
            }
        }

        // All four signals look fine on this check.
        at->state = AUTOTUNE_STATE_WARMING;
        at->consecutive_good_checks++;

        int good_checks_required = GOOD_CHECKS_REQUIRED_NORMAL;
        if (at->extended_freq_mhz > 0.0f) {
            good_checks_required = GOOD_CHECKS_REQUIRED_EXTENDED;
        } else if (in_fast_climb_zone(at, freq_option_count) && !profile->optimize_efficiency) {
            // The shorter confirmation time is purely a speed shortcut
            // (unlike the ASIC-error/hashrate-check exemptions above, which
            // exist because those specific signals are statistically
            // unreliable at low frequency, not because we care less down
            // here -- those stay skipped for every profile regardless).
            // Eco has no reason to rush through this region: taking the
            // normal, longer confirmation time here costs it nothing and
            // gives a more thoroughly-confirmed base to build on, while
            // Performance benefits from getting through the safe, already
            // vendor-tested region quickly so it can spend more of its
            // time actually probing beyond-spec.
            good_checks_required = GOOD_CHECKS_REQUIRED_FAST;
        }

        if (at->consecutive_good_checks < good_checks_required) {
            continue; // not yet confirmed stable -- keep checking
        }
        at->consecutive_good_checks = 0;

        // This combo just proved itself over a full confirmation period --
        // remember it as the point to fall back to if a give-up happens
        // later (see track_extended_climb_failure), rather than some
        // calculated "X MHz lower" guess that was never actually tested.
        at->last_confirmed_extended_freq_mhz = at->extended_freq_mhz;
        at->last_confirmed_voltage_mv = at->voltage_mv;

        // While a give-up cooldown is active, pause exploration entirely --
        // no more climbing (already blocked by next_extended_step_within_ceiling
        // via the ceiling), and no more voltage shaving either. Just hold
        // the restored combo fixed until the cooldown expires; the fast-path
        // safety checks above keep running every cycle regardless.
        bool in_give_up_hold = at->extended_soft_ceiling_mhz > 0.0f &&
                                (esp_timer_get_time() / 1000) < at->extended_soft_ceiling_expiry_ms;

        // Same idea as in_give_up_hold, but for Eco's efficiency peak: once
        // settled there, don't re-probe past it every cycle (see
        // EFFICIENCY_SETTLE_MS) until the hold period is up.
        bool in_efficiency_settle = profile->optimize_efficiency && at->efficiency_settled_expiry_ms > 0 &&
                                     (esp_timer_get_time() / 1000) < at->efficiency_settled_expiry_ms;

        if (at->last_action == AUTOTUNE_ACTION_VOLT_UP) {
            // The extra voltage successfully stabilized this frequency step.
            // Keep it, and go straight back to trying to climb frequency
            // further on the next cycle. This frequency level did ultimately
            // work, so it shouldn't count toward "repeatedly failing here".
            at->extended_freq_consecutive_fails = 0;
            at->last_action = AUTOTUNE_ACTION_NONE;
        } else if (!in_efficiency_settle && at->freq_step_index < freq_option_count - 1 &&
                   (!profile->optimize_efficiency || efficiency_still_improving(at, sys_module, pm, freq_option_count))) {
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
            //
            // One step at a time (get_voltage_step_mv), same as every other
            // voltage change in this file -- this used to jump straight to
            // a proportionally-scaled target, which could be a much bigger
            // single jump than any other voltage change here ever makes
            // (e.g. +150mV in one step right at this zone's boundary).
            if (profile->allow_voltage_rescue && !in_fast_climb_zone(at, freq_option_count)) {
                at->voltage_mv = clamp_voltage(at->voltage_mv + get_voltage_step_mv(at), min_voltage_mv, max_voltage_mv);
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
            if (at->freq_step_index == at->best_efficiency_freq_step) {
                // Back at the peak -- settle here instead of immediately
                // trying to climb past it again next cycle (see
                // in_efficiency_settle / EFFICIENCY_SETTLE_MS).
                at->efficiency_settled_expiry_ms = (esp_timer_get_time() / 1000) + EFFICIENCY_SETTLE_MS;
                ESP_LOGI(TAG, "Settled at peak efficiency (%.0f MHz), holding for an hour before checking for a higher peak again",
                         (float) freq_options[at->freq_step_index]);
            }
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
        } else if (!in_give_up_hold && at->voltage_mv > min_voltage_mv + 0.5f) {
            // The highest frequency we currently trust is maxed out (vendor
            // ceiling, or the beyond-spec ceiling if that's active) and
            // stable. Try shaving voltage down at this same frequency, in
            // fine 10mV steps -- same hashrate, less heat, less power, if
            // it holds.
            at->voltage_mv = clamp_voltage(at->voltage_mv - get_voltage_step_mv(at), min_voltage_mv, max_voltage_mv);
            at->last_action = AUTOTUNE_ACTION_VOLT_DOWN;
            apply_step(GLOBAL_STATE, at, freq_options, at->freq_step_index, at->voltage_mv, at->extended_freq_mhz);
        } else {
            // Highest trusted frequency, lowest voltage that's still stable
            // at it -- this is the best known efficient point (or we're in
            // a post-give-up cooldown hold, deliberately not exploring
            // further right now). Hold here.
            at->state = (at->extended_freq_mhz > 0.0f) ? AUTOTUNE_STATE_BEYOND_SPEC : AUTOTUNE_STATE_AT_CEILING;
            float applied_freq = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : (float) freq_options[at->freq_step_index];
            ESP_LOGI(TAG, "%s%s profile (%.0f MHz @ %.0f mV) and stable, holding",
                     in_give_up_hold ? "In post-failure cooldown for " : "At best known efficient point for ",
                     profile->name, applied_freq, at->voltage_mv);
        }
    }
}
