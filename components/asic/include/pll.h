#ifndef PLL_H_
#define PLL_H_

#include <stdint.h>
#include <stdbool.h>

#define FREQ_MULT 25.0 // MHz

void pll_get_parameters(float target_freq, uint16_t fb_divider_min, uint16_t fb_divider_max, 
                        uint8_t *fb_divider, uint8_t *refdiv, uint8_t *postdiv1, uint8_t *postdiv2,
                        float *actual_freq);

bool pll_get_parameters_bm1370(float target_freq, uint8_t *fb_divider, uint8_t *refdiv,
                              uint8_t *postdiv1, uint8_t *postdiv2, float *actual_freq);

#endif /* PLL_H_ */
