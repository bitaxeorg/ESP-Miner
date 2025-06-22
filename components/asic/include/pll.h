#ifndef PLL_H_
#define PLL_H_

#include <stdint.h>
#include <stdbool.h>
#include "pll_table.h"

bool pll_get_parameters(float target_freq, uint8_t *fb_divider, uint8_t *refdiv,
                        uint8_t *postdiv1, uint8_t *postdiv2, float *actual_freq);

#endif /* PLL_H_ */                               
