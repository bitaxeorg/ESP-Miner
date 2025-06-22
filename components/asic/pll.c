#include <stdbool.h>
#include "pll_table.h"

#include "esp_log.h"

#define TAG "pll"

bool pll_get_parameters(float target_freq, uint8_t *fb_divider, uint8_t *refdiv,
                        uint8_t *postdiv1, uint8_t *postdiv2, float *actual_freq) 
{
    if (target_freq < pll_table[0].freq) {
        ESP_LOGE(TAG, "Didn't find PLL settings for target frequency %g Mhz", target_freq);
        return false;
    }

    int left = 0;
    int right = PLL_TABLE_SIZE - 1;
    int result = -1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        float mid_freq = pll_table[mid].freq;

        if (mid_freq <= target_freq) {
            result = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    *fb_divider = pll_table[result].fb_divider;
    *refdiv = pll_table[result].refdiv;
    *postdiv1 = pll_table[result].postdiv1;
    *postdiv2 = pll_table[result].postdiv2;
    *actual_freq = pll_table[result].freq;
    
    ESP_LOGI(TAG, "Frequency: fb_divider: %d, refdiv: %d, postdiv1: %d, postdiv2: %d, actual: %g MHz", *fb_divider, *refdiv, *postdiv1, *postdiv2, *actual_freq);

    return true;
}
