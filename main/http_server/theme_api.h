#ifndef THEME_API_H
#define THEME_API_H

#include "esp_http_server.h"

// Register theme API endpoints
esp_err_t register_theme_api_endpoints(httpd_handle_t server, void* ctx);


typedef enum {
    THEME_ACS_DEFAULT = 0, 
    THEME_BITAXE_RED = 1,
    THEME_BLOCKSTREAM_JADE = 2,
    THEME_BLOCKSTREAM_BLUE = 3,
    THEME_SOLO_SATOSHI = 4,
    THEME_SOLO_MINING_CO = 5,
    THEME_BTCMAGAZINE = 6,
    THEME_VOSKCOIN = 7,
    THEME_AMERICANBTC = 8,
    THEME_HUT8 = 9,
    THEME_LUXOR = 10,


} themePreset_t;

typedef struct {
    char primaryColor[8];      // Main brand color (e.g. "#FF0000")
    char secondaryColor[8];    // Secondary brand color
    char backgroundColor[8];   // Background color
    char textColor[8];         // Text color
    char borderColor[8];       // Border color
    themePreset_t themePreset;

} uiTheme_t;

extern uiTheme_t* getCurrentTheme(void);
extern themePreset_t getCurrentThemePreset(void);
extern void initializeTheme(themePreset_t preset);
extern themePreset_t loadThemefromNVS(void);


#endif // THEME_API_H

