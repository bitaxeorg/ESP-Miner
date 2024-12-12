#ifndef INPUT_H_
#define INPUT_H_

esp_err_t input_init(void (*button_short_clicked_cb)(void), void (*button_long_pressed_cb)(void));

#endif /* INPUT_H_ */
