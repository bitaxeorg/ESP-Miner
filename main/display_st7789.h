#ifndef DISPLAY_ST7789_H_
#define DISPLAY_ST7789_H_

#include <stdbool.h>
#include "display_driver.h"

typedef struct GlobalState GlobalState;

extern const DisplayDriver display_st7789_driver;

bool display_st7789_probe(GlobalState * GLOBAL_STATE);

#endif /* DISPLAY_ST7789_H_ */
