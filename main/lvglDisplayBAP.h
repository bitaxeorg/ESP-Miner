#ifndef LVGLDISPLAYBAP_H
#define LVGLDISPLAYBAP_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

#include "nvs_config.h"
#include "serial.h"

// Mode Selection
#define LVGL_MODE_BAP  1

// Network data registers (on change only)
#define LVGL_REG_SSID           0x21
#define LVGL_REG_IP_ADDR        0x22
#define LVGL_REG_WIFI_STATUS    0x23
#define LVGL_REG_POOL_URL       0x24
#define LVGL_REG_FALLBACK_URL   0x25
#define LVGL_REG_POOL_PORTS     0x26
#define LVGL_REG_PORTS         0x26

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
#define LVGL_REG_UPTIME         0x45

// Device status registers (on change only)
#define LVGL_REG_FLAGS          0x50

#define LVGL_REG_DEVICE_INFO   0x52
#define LVGL_REG_BOARD_INFO    0x53
#define LVGL_REG_CLOCK_SYNC    0x54

// API data registers (5 second updates)
#define LVGL_REG_API_BTC_PRICE   0x60
#define LVGL_REG_API_NETWORK_HASHRATE 0x61
#define LVGL_REG_API_NETWORK_DIFFICULTY 0x62
#define LVGL_REG_API_BLOCK_HEIGHT 0x63
#define LVGL_REG_API_DIFFICULTY_PROGRESS 0x64
#define LVGL_REG_API_DIFFICULTY_CHANGE 0x65
#define LVGL_REG_API_REMAINING_BLOCKS 0x66
#define LVGL_REG_API_REMAINING_TIME 0x67
#define LVGL_REG_API_FASTEST_FEE 0x68
#define LVGL_REG_API_HALF_HOUR_FEE 0x69
#define LVGL_REG_API_HOUR_FEE 0x6A
#define LVGL_REG_API_ECONOMY_FEE 0x6B
#define LVGL_REG_API_MINIMUM_FEE 0x6C

// Settings registers 
// Network settings
#define LVGL_REG_SETTINGS_HOSTNAME 0x90 // 32 bytes
#define LVGL_REG_SETTINGS_WIFI_SSID 0x91 // 64 bytes
#define LVGL_REG_SETTINGS_WIFI_PASSWORD 0x92 // 64 bytes
// Mining settings
#define LVGL_REG_SETTINGS_STRATUM_URL_MAIN 0x93 // 64 bytes
#define LVGL_REG_SETTINGS_STRATUM_PORT_MAIN 0x94 // 2 bytes
#define LVGL_REG_SETTINGS_STRATUM_USER_MAIN 0x95 // 64 bytes
#define LVGL_REG_SETTINGS_STRATUM_PASSWORD_MAIN 0x96 // 64 bytes
#define LVGL_REG_SETTINGS_STRATUM_URL_FALLBACK 0x97 // 64 bytes
#define LVGL_REG_SETTINGS_STRATUM_PORT_FALLBACK 0x98 // 2 bytes
#define LVGL_REG_SETTINGS_STRATUM_USER_FALLBACK 0x99 // 64 bytes
#define LVGL_REG_SETTINGS_STRATUM_PASSWORD_FALLBACK 0x9A // 64 bytes
// ASIC settings
#define LVGL_REG_SETTINGS_ASIC_VOLTAGE 0x9B // 2 bytes
#define LVGL_REG_SETTINGS_ASIC_FREQ 0x9C // 2 bytes
// Fan settings
#define LVGL_REG_SETTINGS_FAN_SPEED 0x9D // 2 bytes
#define LVGL_REG_SETTINGS_AUTO_FAN_SPEED 0x9E // 2 bytes

// Special registers (0xF0 - 0xFF)
#define LVGL_REG_SPECIAL_RESTART 0xFE

// Flags (0xE0 - 0xEF)
#define LVGL_FLAG_STARTUP_DONE 0xE0
#define LVGL_FLAG_OVERHEAT_MODE 0xE1
#define LVGL_FLAG_FOUND_BLOCK 0xE2



esp_err_t lvglDisplay_initBAP(void);
esp_err_t lvglStartupLoopBAP(GlobalState *GLOBAL_STATE);
esp_err_t lvglOverheatLoopBAP(GlobalState *GLOBAL_STATE);

esp_err_t lvglUpdateDisplayNetworkBAP(GlobalState *GLOBAL_STATE);
esp_err_t lvglUpdateDisplayMiningBAP(GlobalState *GLOBAL_STATE);
esp_err_t lvglUpdateDisplayMonitoringBAP(GlobalState *GLOBAL_STATE);
esp_err_t lvglUpdateDisplayDeviceStatusBAP(GlobalState *GLOBAL_STATE);
esp_err_t lvglUpdateDisplayAPIBAP(void);
esp_err_t lvglGetSettingsBAP(void);

#endif