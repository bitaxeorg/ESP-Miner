#include <stdbool.h>
#include <float.h>
#include <math.h>

#include "pll.h"

#include "esp_log.h"

#define EPSILON 0.0001f

static const char * TAG = "pll";

static bool find_parameters(float target_freq, uint16_t fb_divider_min, uint16_t fb_divider_max, bool bm1370,
                        uint8_t *fb_divider, uint8_t *refdiv, uint8_t *postdiv1, uint8_t *postdiv2,
                        float *actual_freq) 
{
    float best_freq = 0;
    uint8_t best_refdiv = 0, best_fb_divider = 0, best_postdiv1 = 0, best_postdiv2 = 0;
    float min_diff = bm1370 ? 10.0f : FLT_MAX;
    float min_vco_freq = FLT_MAX;
    uint16_t min_postdiv = UINT16_MAX;

    for (uint8_t refdiv = 2; refdiv > 0; refdiv--) {
        for (uint8_t postdiv1 = 7; postdiv1 > 0; postdiv1--) {
            for (uint8_t postdiv2 = 7; postdiv2 > 0; postdiv2--) {
                uint16_t divider = refdiv * postdiv1 * postdiv2;
                double rounded_fb = round(target_freq / FREQ_MULT * divider);
                if (!isfinite(rounded_fb) || rounded_fb < fb_divider_min || rounded_fb > fb_divider_max) {
                    continue;
                }
                uint16_t fb_divider = rounded_fb;
                if (postdiv1 > postdiv2 &&
                    fb_divider >= fb_divider_min && fb_divider <= fb_divider_max) {
                    float new_freq = FREQ_MULT * fb_divider / divider;
                    float curr_diff = fabs(target_freq - new_freq);
                    float vco_freq = FREQ_MULT * fb_divider / refdiv;
                    // BM1370 limits from cminer's BM1370 set_frequency (0x503ec).
                    if (bm1370 && (vco_freq < 2000.0f || vco_freq >= 3200.0f ||
                                   (refdiv == 1 && vco_freq > 3125.0f))) {
                        continue;
                    }
                    // Prioritize: 
                    // 1. Closest frequency to target
                    // 2. Lowest VCO frequency
                    // 3. Lowest postdiv1 * postdiv2
                    if (curr_diff < min_diff ||
                       (fabs(curr_diff - min_diff) < EPSILON && vco_freq < min_vco_freq) ||
                       (fabs(curr_diff - min_diff) < EPSILON && fabs(vco_freq - min_vco_freq) < EPSILON && postdiv1 * postdiv2 < min_postdiv)) {
                        min_diff = curr_diff;
                        min_vco_freq = vco_freq;
                        min_postdiv = postdiv1 * postdiv2;
                        best_freq = new_freq;
                        best_refdiv = refdiv;
                        best_fb_divider = fb_divider;
                        best_postdiv1 = postdiv1;
                        best_postdiv2 = postdiv2;
                    }
                }
            }
        }
    }

    ESP_LOGI(TAG, "Frequency: %g MHz (fb_divider: %d, refdiv: %d, postdiv1: %d, postdiv2: %d)", best_freq, best_fb_divider, best_refdiv, best_postdiv1, best_postdiv2);

    *actual_freq = best_freq;
    *fb_divider = best_fb_divider;
    *refdiv = best_refdiv;
    *postdiv1 = best_postdiv1;
    *postdiv2 = best_postdiv2;
    return best_refdiv != 0;
}

void pll_get_parameters(float target_freq, uint16_t fb_divider_min, uint16_t fb_divider_max,
                        uint8_t *fb_divider, uint8_t *refdiv, uint8_t *postdiv1, uint8_t *postdiv2,
                        float *actual_freq)
{
    find_parameters(target_freq, fb_divider_min, fb_divider_max, false,
                    fb_divider, refdiv, postdiv1, postdiv2, actual_freq);
}

bool pll_get_parameters_bm1370(float target_freq, uint8_t *fb_divider, uint8_t *refdiv,
                              uint8_t *postdiv1, uint8_t *postdiv2, float *actual_freq)
{
    return find_parameters(target_freq, 16, 250, true,
                           fb_divider, refdiv, postdiv1, postdiv2, actual_freq);
}
