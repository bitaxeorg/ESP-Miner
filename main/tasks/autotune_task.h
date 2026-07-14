#ifndef AUTOTUNE_TASK_H_
#define AUTOTUNE_TASK_H_

typedef enum {
    AUTOTUNE_PROFILE_EFFICIENCY = 0, // stops climbing at peak hash/watt -- see optimize_efficiency
    AUTOTUNE_PROFILE_PERFORMANCE,    // climbs to the vendor ceiling, and past it too if Custom Settings is unlocked
} AutotuneProfile;

typedef enum {
    AUTOTUNE_STATE_IDLE = 0,      // disabled via NVS
    AUTOTUNE_STATE_PAUSED,        // waiting out an overheat/pause/fault condition
    AUTOTUNE_STATE_WARMING,       // settled on a step, collecting samples
    AUTOTUNE_STATE_AT_CEILING,    // sitting at the highest vendor-approved step
    AUTOTUNE_STATE_BEYOND_SPEC,   // exploring past the vendor table (opt-in only)
} AutotuneState;

// What the last applied step actually was, so an unstable result can be
// reverted correctly -- climbing frequency reverts by lowering frequency,
// but an undervolt trial reverts by raising voltage back up, not by
// touching frequency at all.
typedef enum {
    AUTOTUNE_ACTION_NONE = 0,
    AUTOTUNE_ACTION_FREQ_UP,
    AUTOTUNE_ACTION_VOLT_UP,      // tried extra voltage to stabilize a frequency step
    AUTOTUNE_ACTION_VOLT_DOWN,    // undervolt trial: same frequency, less voltage
} AutotuneAction;

typedef struct {
    AutotuneState state;
    AutotuneAction last_action;
    AutotuneProfile active_profile;
    int freq_step_index;
    // Continuously tracked in mV (10mV resolution) rather than an index into
    // the vendor table's handful of discrete entries, so the tuner can
    // settle much closer to the true minimum stable voltage. Still always
    // clamped to the vendor table's own lowest/highest entries.
    float voltage_mv;
    // Only used once freq_step_index is at the top of the vendor table AND
    // the user has explicitly unlocked custom settings (overclockEnabled).
    // Voltage never extends past the vendor table; only frequency does.
    float extended_freq_mhz;
    // If beyond-spec climbing keeps failing near the same frequency
    // instead of making progress, this caps further climbing there so the
    // tuner falls through to voltage optimization instead of retrying the
    // same failing region forever. Expires after EXTENDED_COOLDOWN_MS, so
    // the tuner will go back and probe that frequency again later (ambient
    // conditions change) rather than avoiding it for the rest of the session.
    float extended_soft_ceiling_mhz;
    int64_t extended_soft_ceiling_expiry_ms;
    float extended_last_failed_mhz;
    // The most recent frequency/voltage combo that actually survived a
    // full confirmation period (see GOOD_CHECKS_REQUIRED_*). When the
    // tuner gives up on a beyond-spec excursion after repeated failures,
    // it falls back to exactly this -- not a calculated "a bit lower"
    // guess -- and holds it fixed for the cooldown period rather than
    // continuing to explore. 0 means nothing beyond the vendor table has
    // been confirmed yet this session.
    float last_confirmed_extended_freq_mhz;
    float last_confirmed_voltage_mv;
    int extended_freq_consecutive_fails;
    float max_temp_seen_this_window;
    // Set every time a step is applied, so the fast-path safety checks can
    // give the chip a brief moment to settle (PLL relock, voltage regulator
    // ramp) before judging it -- without this, a normal settling transient
    // right after a big frequency/voltage change can look identical to
    // genuine instability, most notably on the very first step out of a
    // fresh (re-)enable, where there's nowhere lower left to retreat to if
    // that transient gets misread as "this step doesn't work".
    int64_t step_applied_at_ms;
    // Only used by profiles with optimize_efficiency set (currently Eco).
    // Tracks hash-per-watt at the best frequency step found so far, so
    // climbing can stop at the efficiency peak instead of the thermal or
    // stability ceiling -- those are usually not the same point, since
    // voltage requirements tend to grow faster than frequency near the top.
    float best_efficiency_ghs_per_watt;
    int best_efficiency_freq_step;
    // Once the efficiency peak is found and the tuner has retreated back
    // to it, this holds it there for a while instead of re-probing past
    // it every single confirmation cycle -- without this, ordinary
    // measurement noise in hash/watt could occasionally look like "still
    // improving" and trigger another climb-then-retreat that never
    // actually finds anything better. 0 means not currently settled.
    int64_t efficiency_settled_expiry_ms;
    // Counts consecutive periodic checks (10s apart) where a given signal
    // read as bad. current_hashrate and error_percentage are both
    // short-interval instantaneous readings with real sample-to-sample
    // variance, especially at low absolute hashrates -- requiring a signal
    // to persist across a couple of checks filters that noise out while
    // still catching a genuinely sustained problem quickly. Temperature and
    // input voltage don't get this treatment: those are direct physical
    // readings that react immediately, on purpose.
    int hashrate_shortfall_ticks;
    int asic_error_bad_ticks;
    // How many consecutive clean checks (all signals fine) have been seen
    // at the current step -- once this reaches the profile/zone's required
    // count, the step is treated as confirmed stable and the tuner decides
    // what to do next (climb further, shave voltage, hold, etc).
    int consecutive_good_checks;
    int step_downs_total;
    int step_ups_total;
} AutotuneModule;

void AUTOTUNE_task(void * pvParameters);

#endif