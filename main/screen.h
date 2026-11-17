#ifndef SCREEN_H_
#define SCREEN_H_

#include "esp_err.h"

#define MAX_CAROUSEL_SCREENS 8
#define MAX_CAROUSEL_LABELS 16

typedef struct GlobalState GlobalState;

esp_err_t screen_start(GlobalState * GLOBAL_STATE);
void screen_button_press(void);

#endif /* SCREEN_H_ */
