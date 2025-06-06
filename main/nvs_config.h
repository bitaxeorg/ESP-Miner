#ifndef MAIN_NVS_CONFIG_H
#define MAIN_NVS_CONFIG_H

#include <stdint.h>

// Max length 15

#define NVS_CONFIG_WIFI_SSID "wifissid"
#define NVS_CONFIG_WIFI_PASS "wifipass"
#define NVS_CONFIG_HOSTNAME "hostname"
#define NVS_CONFIG_STRATUM_URL "stratumurl"
#define NVS_CONFIG_STRATUM_PORT "stratumport"
#define NVS_CONFIG_FALLBACK_STRATUM_URL "fbstratumurl"
#define NVS_CONFIG_FALLBACK_STRATUM_PORT "fbstratumport"
#define NVS_CONFIG_STRATUM_USER "stratumuser"
#define NVS_CONFIG_STRATUM_PASS "stratumpass"
#define NVS_CONFIG_FALLBACK_STRATUM_USER "fbstratumuser"
#define NVS_CONFIG_FALLBACK_STRATUM_PASS "fbstratumpass"
#define NVS_CONFIG_ASIC_FREQ "asicfrequency"
#define NVS_CONFIG_ASIC_VOLTAGE "asicvoltage"
#define NVS_CONFIG_ASIC_MODEL "asicmodel"
#define NVS_CONFIG_DEVICE_MODEL "devicemodel"
#define NVS_CONFIG_BOARD_VERSION "boardversion"
#define NVS_CONFIG_FLIP_SCREEN "flipscreen"
#define NVS_CONFIG_INVERT_SCREEN "invertscreen"
#define NVS_CONFIG_INVERT_FAN_POLARITY "invertfanpol"
#define NVS_CONFIG_AUTO_FAN_SPEED "autofanspeed"
#define NVS_CONFIG_FAN_SPEED "fanspeed"
#define NVS_CONFIG_BEST_DIFF "bestdiff"
#define NVS_CONFIG_SELF_TEST "selftest"
#define NVS_CONFIG_OVERHEAT_MODE "overheat_mode"
#define NVS_CONFIG_SWARM "swarmconfig"

// Theme configuration
#define NVS_CONFIG_THEME_NAME "themename"

// Autotune configuration
#define NVS_CONFIG_AUTOTUNE_FLAG "autotune"
#define NVS_CONFIG_AUTOTUNE_PRESET "preset"

// Warranty Checks
#define NVS_CONFIG_SERIAL_NUMBER "serialnumber"
#define NVS_CONFIG_BUILD_DATE "builddate"
#define NVS_CONFIG_HIGHEST_FREQUENCY "highfreq"
#define NVS_CONFIG_HIGHEST_DOMAIN_VOLTAGE "highdomvolt"
#define NVS_CONFIG_HIGHEST_TEMPERATURE "hightemp"
#define NVS_CONFIG_OVERHEAT_COUNT "overheat"
#define NVS_CONFIG_OVERVOLT_COUNT "overvolt"
#define NVS_CONFIG_OVERPOWER_COUNT "overpower"





char * nvs_config_get_string(const char * key, const char * default_value);
void nvs_config_set_string(const char * key, const char * default_value);
uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value);
void nvs_config_set_u16(const char * key, const uint16_t value);
uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value);
void nvs_config_set_u64(const char * key, const uint64_t value);

#endif // MAIN_NVS_CONFIG_H
