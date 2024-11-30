#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "screen.h"
#include "connect.h"

#define BUTTON_BOOT_GPIO       GPIO_NUM_0
#define ESP_INTR_FLAG_DEFAULT  0
#define LONG_PRESS_DURATION_MS 2000

static const char * TAG = "input";

static lv_indev_state_t button_state = LV_INDEV_STATE_RELEASED;
static lv_point_t points[] = { {0, 0} }; // must be static

static void button_read(lv_indev_t *indev, lv_indev_data_t *data) 
{
    data->key = LV_KEY_ENTER;
    data->state = button_state;
}

static void IRAM_ATTR button_isr_handler(void *arg) 
{
    bool pressed = gpio_get_level(BUTTON_BOOT_GPIO) == 0; // LOW when pressed
    button_state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void button_short_clicked(lv_event_t *e)
{
    ESP_LOGI(TAG, "Short button press detected, switching to next screen");
    screen_next();
}

static void button_long_pressed(lv_event_t *e)
{
    ESP_LOGI(TAG, "Long button press detected, toggling WiFi SoftAP");
    toggle_wifi_softap();
}

esp_err_t input_init(void)
{
    // Button handling
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io_conf);

    // Install ISR service and hook the interrupt handler
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT), TAG, "Error installing ISR service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON_BOOT_GPIO, button_isr_handler, NULL), TAG, "Error adding ISR handler");

    // Create input device
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_BUTTON);
    lv_indev_set_long_press_time(indev, LONG_PRESS_DURATION_MS);
    lv_indev_set_read_cb(indev, button_read);
    lv_indev_set_button_points(indev, points);
    lv_indev_add_event_cb(indev, button_short_clicked, LV_EVENT_SHORT_CLICKED, NULL);
    lv_indev_add_event_cb(indev, button_long_pressed, LV_EVENT_LONG_PRESSED, NULL);

    return ESP_OK;
}
