#ifndef DISPLAY_OLED_H_
#define DISPLAY_OLED_H_

#include <stdbool.h>
#include "display_driver.h"

typedef struct GlobalState GlobalState;

extern const DisplayDriver display_oled_driver;

bool display_oled_probe(void);

#endif /* DISPLAY_OLED_H_ */
