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
#define NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE "stratumxnsub"
#define NVS_CONFIG_STRATUM_DIFFICULTY "stratumdiff"
#define NVS_CONFIG_STRATUM_PASS "stratumpass"
#define NVS_CONFIG_FALLBACK_STRATUM_USER "fbstratumuser"
#define NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE "stratumfbxnsub"
#define NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY "fbstratumdiff"
#define NVS_CONFIG_FALLBACK_STRATUM_PASS "fbstratumpass"
#define NVS_CONFIG_ASIC_FREQUENCY "asicfrequency"
#define NVS_CONFIG_ASIC_FREQUENCY_FLOAT "asicfrequency_f"
#define NVS_CONFIG_ASIC_VOLTAGE "asicvoltage"
#define NVS_CONFIG_ASIC_MODEL "asicmodel"
#define NVS_CONFIG_DEVICE_MODEL "devicemodel"
#define NVS_CONFIG_BOARD_VERSION "boardversion"
#define NVS_CONFIG_DISPLAY "display"
#define NVS_CONFIG_ROTATION "rotation"
#define NVS_CONFIG_INVERT_SCREEN "invertscreen"
#define NVS_CONFIG_DISPLAY_TIMEOUT "displayTimeout"
#define NVS_CONFIG_AUTO_FAN_SPEED "autofanspeed"
#define NVS_CONFIG_FAN_SPEED "fanspeed"
#define NVS_CONFIG_MIN_FAN_SPEED "minfanspeed"
#define NVS_CONFIG_TEMP_TARGET "temptarget"
#define NVS_CONFIG_BEST_DIFF "bestdiff"
#define NVS_CONFIG_SELF_TEST "selftest"
#define NVS_CONFIG_OVERHEAT_MODE "overheat_mode"
#define NVS_CONFIG_OVERCLOCK_ENABLED "oc_enabled"
#define NVS_CONFIG_SWARM "swarmconfig"
#define NVS_CONFIG_STATISTICS_FREQUENCY "statsFrequency"

// Theme configuration
#define NVS_CONFIG_THEME_SCHEME "themescheme"
#define NVS_CONFIG_THEME_COLORS "themecolors"

// Device config overrides
#define NVS_CONFIG_PLUG_SENSE "plug_sense"
#define NVS_CONFIG_ASIC_ENABLE "asic_enable"
#define NVS_CONFIG_EMC2101 "EMC2101"
#define NVS_CONFIG_EMC2103 "EMC2103"
#define NVS_CONFIG_EMC_INTERNAL_TEMP "emc_int_temp"
#define NVS_CONFIG_EMC_IDEALITY_FACTOR "emc_ideality_f"
#define NVS_CONFIG_EMC_BETA_COMPENSATION "emc_beta_comp"
#define NVS_CONFIG_EMC_TEMP_OFFSET "emc_temp_offset"
#define NVS_CONFIG_DS4432U "DS4432U"
#define NVS_CONFIG_INA260 "INA260"
#define NVS_CONFIG_TPS546 "TPS546"
#define NVS_CONFIG_POWER_CONSUMPTION_TARGET "power_cons_tgt"

char * nvs_config_get_string(const char * key, const char * default_value);
void nvs_config_set_string(const char * key, const char * default_value);
uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value);
void nvs_config_set_u16(const char * key, const uint16_t value);
int32_t nvs_config_get_i32(const char * key, const int32_t default_value);
void nvs_config_set_i32(const char * key, const int32_t value);
uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value);
void nvs_config_set_u64(const char * key, const uint64_t value);
float nvs_config_get_float(const char *key, float default_value);
void nvs_config_set_float(const char *key, float value);
void nvs_config_commit(void);

#endif // MAIN_NVS_CONFIG_H
