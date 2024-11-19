#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

#include "nvs_config.h"
#include "i2c_bitaxe.h"

// Add these register definitions
#define LVGL_REG_SSID           0x21
#define LVGL_REG_IP_ADDR        0x22
#define LVGL_REG_WIFI_STATUS    0x23
#define LVGL_REG_POOL_URL       0x24
#define LVGL_REG_FALLBACK_URL   0x25
#define LVGL_REG_POOL_PORTS     0x26

// Mining data registers (5 second updates)
#define LVGL_REG_HASHRATE        0x30
#define LVGL_REG_HIST_HASHRATE   0x31
#define LVGL_REG_EFFICIENCY      0x32
#define LVGL_REG_BEST_DIFF       0x33
#define LVGL_REG_SESSION_DIFF    0x34
#define LVGL_REG_SHARES          0x35

// Monitoring data registers (5 second updates)
#define LVGL_REG_TEMPS           0x40
#define LVGL_REG_ASIC_FREQ      0x41
#define LVGL_REG_FAN            0x42
#define LVGL_REG_POWER_STATS    0x43
#define LVGL_REG_ASIC_INFO      0x44

// Device status registers (on change only)
#define LVGL_REG_FLAGS          0x50
#define LVGL_REG_UPTIME        0x51
#define LVGL_REG_DEVICE_INFO   0x52
#define LVGL_REG_BOARD_INFO    0x53

esp_err_t lvglDisplay_init(void);
esp_err_t lvglUpdateDisplayValues(GlobalState *GLOBAL_STATE);
esp_err_t lvglUpdateDisplayMining(GlobalState *GLOBAL_STATE);
esp_err_t lvglUpdateDisplayMonitoring(GlobalState *GLOBAL_STATE);
#endif