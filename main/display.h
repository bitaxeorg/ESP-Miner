#ifndef DISPLAY_H_
#define DISPLAY_H_

#define DEFAULT_DISPLAY "SSD1306 (128x32)"

typedef enum
{
    NONE,
    SSD1306,
    SSD1309,
    SH1107,
} Display;

typedef struct {
    const char * name;
    Display display;
    uint16_t h_res;
    uint16_t v_res;
} DisplayConfig;

static const DisplayConfig display_configs[] = {
    { .name = "NONE",             .display = NONE,                                },
    { .name = DEFAULT_DISPLAY,    .display = SSD1306, .h_res = 128, .v_res = 32,  },
    { .name = "SSD1309 (128x64)", .display = SSD1309, .h_res = 128, .v_res = 64,  },
    { .name = "SH1107 (64x128)",  .display = SH1107,  .h_res = 64,  .v_res = 128, },
    { .name = "SH1107 (128x128)", .display = SH1107,  .h_res = 128, .v_res = 128, },
};

esp_err_t display_init(void * pvParameters);
esp_err_t display_on(bool display_on);
const DisplayConfig * get_display_config(const char * name);

#endif /* DISPLAY_H_ */
