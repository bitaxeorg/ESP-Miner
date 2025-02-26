#ifndef SCREEN_H_
#define SCREEN_H_

typedef enum {
    SCR_SELF_TEST,
    SCR_OVERHEAT,
    SCR_INVALID_ASIC,
    SCR_CONFIGURE,
    SCR_FIRMWARE_UPDATE,
    SCR_CONNECTION,
    SCR_LOGO,
    SCR_URLS,
    SCR_STATS,
    MAX_SCREENS,
} screen_t;

#define SCR_CAROUSEL_START SCR_URLS
#define SCR_CAROUSEL_END SCR_STATS

esp_err_t screen_start(void * pvParameters);
void screen_next(void);

#endif /* SCREEN_H_ */
