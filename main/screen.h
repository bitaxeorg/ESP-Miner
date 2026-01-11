#ifndef SCREEN_H_
#define SCREEN_H_

#define MAX_CAROUSEL_SCREENS 8
#define MAX_CAROUSEL_LABELS 16

esp_err_t screen_start(void * pvParameters);
void screen_button_press(void);

#endif /* SCREEN_H_ */
