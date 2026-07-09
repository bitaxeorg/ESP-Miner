#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "autotune_task.h"

static const char * TAG = "autotune";

// --- Tuning behaviour ("maximaal": ride close to the limits, keep re-checking forever) ---

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

// Safety margin below the power-management task's *reactive* hard cutoff
// (PM_THROTTLE_TEMP). That reactive cutoff stops mining and drops to reduced
// settings -- this tuner's job is to proactively back off *before* that ever
// triggers, while still sitting as close to it as is reasonably safe.
#define TEMP_MARGIN_C 2.0
#define TEMP_CEILING_C (PM_THROTTLE_TEMP - TEMP_MARGIN_C)

// If rejected shares exceed this fraction of shares seen in a window, the
// current step is considered unstable and we back off immediately.
#define REJECT_RATE_MAX 0.03f

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
// applies to the vendor table's own highest frequency. This number is not
// exposed as a setting anywhere -- it exists so "opt-in to explore beyond
// spec" can never become "opt-in to unbounded frequency". Used as a fallback
// for chips without a known-safe ceiling below.
#define EXTENDED_MAX_MULTIPLIER 1.15f

// Per-chip ceilings the person has personally run stably via manual
// overclock. A real data point from someone's own hardware is more
// trustworthy than a generic percentage guess, so these override the
// percentage-based ceiling for that specific chip. Only add an entry here
// once it's been personally verified stable -- this is still a hard,
// non-configurable ceiling, just a chip-specific one instead of a generic one.
static float get_known_safe_ceiling_mhz(Asic asic_id)
{
    switch (asic_id) {
        case BM1370: return 800.0f; // user-reported stable via manual OC
        default:     return 0.0f;   // no personal data point yet -- use percentage-based cap
    }
}

// Beyond-spec steps are unvalidated, so we require a longer, cleaner window
// before trusting a step as stable -- more shares, and a still-lower reject
// tolerance than the standard vendor-table climbing uses.
#define EXTENDED_MIN_SAMPLE_SHARES 30
#define EXTENDED_REJECT_RATE_MAX 0.015f

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

static void apply_step(GlobalState * GLOBAL_STATE, const uint16_t * freq_options, const uint16_t * volt_options,
                        int freq_idx, int volt_idx, float extended_freq_mhz)
{
    // extended_freq_mhz > 0 means we're operating past the vendor table --
    // that value wins over whatever the table index says. Voltage always
    // comes from the vendor table regardless of extended mode.
    float new_freq = extended_freq_mhz > 0.0f ? extended_freq_mhz : (float) freq_options[freq_idx];
    uint16_t new_volt = volt_options[volt_idx];

    ESP_LOGI(TAG, "Applying step: %.0f MHz @ %u mV%s", new_freq, new_volt,
             extended_freq_mhz > 0.0f ? " (beyond vendor spec)" : "");

    nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, new_freq);
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, new_volt);
    // power_management_task polls these NVS values on its own ~100ms loop and
    // applies them to the ASIC; we don't touch the hardware directly here so
    // there's a single owner of "what the ASIC is currently told to do".
}

static void resync_from_nvs(AutotuneModule * at, const uint16_t * freq_options, const uint16_t * volt_options,
                             int freq_option_count)
{
    float current_freq = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    at->freq_step_index = find_index(freq_options, (uint16_t) current_freq);
    at->volt_step_index = find_index(volt_options, nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE));

    float vendor_max_freq = (float) freq_options[freq_option_count - 1];
    at->extended_freq_mhz = (current_freq > vendor_max_freq + 0.5f) ? current_freq : 0.0f;
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

    at->state = AUTOTUNE_STATE_IDLE;
    resync_from_nvs(at, freq_options, volt_options, freq_option_count);
    at->step_downs_total = 0;
    at->step_ups_total = 0;

    uint64_t window_start_accepted = 0;
    uint64_t window_start_rejected = 0;
    int64_t window_start_ms = 0;
    float window_max_temp = 0.0f;
    bool window_open = false;

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
            resync_from_nvs(at, freq_options, volt_options, freq_option_count);
            continue;
        }

        float chip_temp = pm->chip_temp_avg;
        if (pm->chip_temp2_avg > chip_temp) {
            chip_temp = pm->chip_temp2_avg;
        }

        if (!window_open) {
            window_start_accepted = sys_module->shares_accepted;
            window_start_rejected = sys_module->shares_rejected;
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
        // we're already over the ceiling. This is what keeps "maximal" mode
        // from turning into "reactive overheat-protection kicks in instead".
        if (window_max_temp > TEMP_CEILING_C) {
            ESP_LOGW(TAG, "Temp %.1fC over ceiling %.1fC mid-window, stepping down early",
                     window_max_temp, TEMP_CEILING_C);

            if (at->extended_freq_mhz > 0.0f) {
                at->extended_freq_mhz -= EXTENDED_STEP_MHZ;
                if (at->extended_freq_mhz <= (float) freq_options[freq_option_count - 1] + 0.5f) {
                    at->extended_freq_mhz = 0.0f; // back into vendor-tested territory
                }
            } else if (at->freq_step_index > 0) {
                at->freq_step_index--;
            } else if (at->volt_step_index > 0) {
                at->volt_step_index--;
            }
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, freq_options, volt_options, at->freq_step_index, at->volt_step_index, at->extended_freq_mhz);
            window_open = false;
            continue;
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - window_start_ms < EVAL_WINDOW_MS) {
            continue; // still collecting samples for this step
        }

        uint64_t accepted_delta = sys_module->shares_accepted - window_start_accepted;
        uint64_t rejected_delta = sys_module->shares_rejected - window_start_rejected;
        uint64_t total_delta = accepted_delta + rejected_delta;

        // Beyond-spec steps get a stricter bar: more samples required, lower
        // tolerance for rejects, since these values are unvalidated by the vendor.
        int min_samples = at->extended_freq_mhz > 0.0f ? EXTENDED_MIN_SAMPLE_SHARES : MIN_SAMPLE_SHARES;
        float reject_threshold = at->extended_freq_mhz > 0.0f ? EXTENDED_REJECT_RATE_MAX : REJECT_RATE_MAX;

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
            ESP_LOGW(TAG, "Reject rate %.1f%% over threshold at step %d/%d%s, stepping down",
                     reject_rate * 100.0f, at->freq_step_index, at->volt_step_index,
                     at->extended_freq_mhz > 0.0f ? " (beyond spec)" : "");

            if (at->extended_freq_mhz > 0.0f) {
                at->extended_freq_mhz -= EXTENDED_STEP_MHZ;
                if (at->extended_freq_mhz <= (float) freq_options[freq_option_count - 1] + 0.5f) {
                    at->extended_freq_mhz = 0.0f; // back into vendor-tested territory
                }
            } else if (at->freq_step_index > 0) {
                at->freq_step_index--;
            } else if (at->volt_step_index > 0) {
                at->volt_step_index--;
            }
            at->step_downs_total++;
            apply_step(GLOBAL_STATE, freq_options, volt_options, at->freq_step_index, at->volt_step_index, at->extended_freq_mhz);
        } else if (at->freq_step_index < freq_option_count - 1) {
            at->freq_step_index++;
            at->step_ups_total++;
            at->state = AUTOTUNE_STATE_WARMING;
            apply_step(GLOBAL_STATE, freq_options, volt_options, at->freq_step_index, at->volt_step_index, 0.0f);
        } else if (at->volt_step_index < volt_option_count - 1) {
            // Frequency is already at the vendor-approved ceiling; a bit more
            // voltage headroom can sometimes buy back stability margin.
            at->volt_step_index++;
            at->step_ups_total++;
            at->state = AUTOTUNE_STATE_WARMING;
            apply_step(GLOBAL_STATE, freq_options, volt_options, at->freq_step_index, at->volt_step_index, 0.0f);
        } else if (nvs_config_get_bool(NVS_CONFIG_OVERCLOCK_ENABLED)) {
            // At the top of the vendor table for both frequency and voltage,
            // and the user has explicitly unlocked custom settings in the UI
            // (the same signal that exposes raw frequency/voltage entry).
            // Keep exploring frequency alone, in small steps, up to a fixed
            // hard ceiling relative to the vendor's own max. Voltage never
            // moves past its vendor-max here.
            float vendor_max_freq = (float) freq_options[freq_option_count - 1];
            float known_ceiling = get_known_safe_ceiling_mhz(GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            float extended_ceiling = known_ceiling > 0.0f ? known_ceiling : (vendor_max_freq * EXTENDED_MAX_MULTIPLIER);
            float base = at->extended_freq_mhz > 0.0f ? at->extended_freq_mhz : vendor_max_freq;
            float next_freq = base + EXTENDED_STEP_MHZ;

            if (next_freq <= extended_ceiling) {
                at->extended_freq_mhz = next_freq;
                at->step_ups_total++;
                at->state = AUTOTUNE_STATE_BEYOND_SPEC;
                apply_step(GLOBAL_STATE, freq_options, volt_options, at->freq_step_index, at->volt_step_index, at->extended_freq_mhz);
            } else {
                at->state = AUTOTUNE_STATE_BEYOND_SPEC;
                ESP_LOGI(TAG, "At beyond-spec ceiling (%.0f MHz, ceiling %.0f MHz) and stable, holding",
                         at->extended_freq_mhz, extended_ceiling);
            }
        } else {
            // Already at the highest frequency AND voltage this ASIC's
            // vendor-tested table exposes, and custom settings aren't
            // unlocked -- hold here rather than guess at unvalidated values.
            at->state = AUTOTUNE_STATE_AT_CEILING;
            ESP_LOGI(TAG, "At vendor-max settings (%.0f MHz @ %u mV) and stable, holding",
                     (float) freq_options[at->freq_step_index], volt_options[at->volt_step_index]);
        }

        // Continuous mode: re-open a fresh window immediately so we keep
        // re-checking (and can climb back up if e.g. ambient temp drops, or
        // step down again if it rises) for as long as autotune is enabled.
        window_open = false;
    }
}
