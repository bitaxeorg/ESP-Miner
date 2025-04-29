#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum
{
    BM1397,
    BM1366,
    BM1368,
    BM1370,
} Model;

typedef enum
{
    NONE,
    SSD1306,
    SH1307,
} Display;

typedef struct {
    Model model;
    const char * name;
    uint16_t chip_id;
    uint16_t default_frequency_mhz;
    uint16_t default_voltage_mv;
    uint16_t hashrate_target;
    uint16_t difficulty;
    uint16_t core_count;
    uint16_t small_core_count;
    // test values
    float hashrate_test_percentage_target;
} AsicConfig;

typedef struct {
    const char * name;
    AsicConfig asic;
    uint8_t asic_count;
    uint16_t max_power;
    uint16_t power_offset;
    uint16_t nominal_voltage;
} FamilyConfig;

typedef struct {
    const char * board_version;
    FamilyConfig family;
    Display display;
    bool plug_sense;
    bool asic_enable;
    bool EMC2101 : 1;
    bool EMC2103 : 1;
    bool emc_internal_temp : 1;
    uint8_t emc_ideality_factor;
    uint8_t emc_beta_compensation;
    bool DS4432U : 1;
    bool INA260  : 1;
    bool TPS546  : 1;
    // test values
    uint16_t power_consumption_target;
} DeviceConfig;

static const AsicConfig ASIC_BM1397 = { .model = BM1397, .name = "BM1397", .chip_id = 1397, .default_frequency_mhz = 425, .default_voltage_mv = 1400, .difficulty = 256, .core_count = 168, .small_core_count =  672, .hashrate_test_percentage_target = 0.85, };
static const AsicConfig ASIC_BM1366 = { .model = BM1366, .name = "BM1366", .chip_id = 1366, .default_frequency_mhz = 486, .default_voltage_mv = 1200, .difficulty = 256, .core_count = 112, .small_core_count =  894, .hashrate_test_percentage_target = 0.85, };
static const AsicConfig ASIC_BM1368 = { .model = BM1368, .name = "BM1368", .chip_id = 1368, .default_frequency_mhz = 490, .default_voltage_mv = 1166, .difficulty = 256, .core_count =  80, .small_core_count = 1276, .hashrate_test_percentage_target = 0.80, };
static const AsicConfig ASIC_BM1370 = { .model = BM1370, .name = "BM1370", .chip_id = 1370, .default_frequency_mhz = 525, .default_voltage_mv = 1150, .difficulty = 256, .core_count = 128, .small_core_count = 2040, .hashrate_test_percentage_target = 0.85, };

static const FamilyConfig FAMILY_MAX         = { .name = "Max",        .asic = ASIC_BM1397, .asic_count = 1, .max_power = 25, .power_offset = 5,  .nominal_voltage = 5,  };
static const FamilyConfig FAMILY_ULTRA       = { .name = "Ultra",      .asic = ASIC_BM1366, .asic_count = 1, .max_power = 25, .power_offset = 5,  .nominal_voltage = 5,  };
static const FamilyConfig FAMILY_HEX         = { .name = "Hex",        .asic = ASIC_BM1366, .asic_count = 6, .max_power = 0,  .power_offset = 5,  .nominal_voltage = 5,  };
static const FamilyConfig FAMILY_SUPRA       = { .name = "Supra",      .asic = ASIC_BM1368, .asic_count = 1, .max_power = 40, .power_offset = 5,  .nominal_voltage = 5,  };
static const FamilyConfig FAMILY_GAMMA       = { .name = "Gamma",      .asic = ASIC_BM1370, .asic_count = 1, .max_power = 40, .power_offset = 5,  .nominal_voltage = 5,  };
static const FamilyConfig FAMILY_GAMMA_TURBO = { .name = "GammaTurbo", .asic = ASIC_BM1370, .asic_count = 2, .max_power = 60, .power_offset = 10, .nominal_voltage = 12, };

static const DeviceConfig default_configs[] = {
    { .board_version = "0.11", .family = FAMILY_MAX,         .display = SSD1306, .EMC2101 = true,                                                             .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "102",  .family = FAMILY_MAX,         .display = SSD1306, .EMC2101 = true,                                                             .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "2.2",  .family = FAMILY_ULTRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "201",  .family = FAMILY_ULTRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "202",  .family = FAMILY_ULTRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "203",  .family = FAMILY_ULTRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "204",  .family = FAMILY_ULTRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true,                      .power_consumption_target = 12, },
    { .board_version = "205",  .family = FAMILY_ULTRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "400",  .family = FAMILY_SUPRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "401",  .family = FAMILY_SUPRA,       .display = SSD1306, .EMC2101 = true, .emc_internal_temp = true,                                  .DS4432U = true, .INA260 = true, .plug_sense = true, .asic_enable = true, .power_consumption_target = 12, },
    { .board_version = "402",  .family = FAMILY_SUPRA,       .display = SSD1306, .EMC2101 = true,                                                             .TPS546 = true,                                                           .power_consumption_target = 5,  },
    { .board_version = "403",  .family = FAMILY_SUPRA,       .display = SSD1306, .EMC2101 = true,                                                             .TPS546 = true,                                                           .power_consumption_target = 5,  },
    { .board_version = "600",  .family = FAMILY_GAMMA,       .display = SSD1306, .EMC2101 = true, .emc_ideality_factor = 0x24, .emc_beta_compensation = 0x00, .TPS546 = true,                                                           .power_consumption_target = 19, },
    { .board_version = "601",  .family = FAMILY_GAMMA,       .display = SSD1306, .EMC2101 = true, .emc_ideality_factor = 0x24, .emc_beta_compensation = 0x00, .TPS546 = true,                                                           .power_consumption_target = 19, },
    { .board_version = "602",  .family = FAMILY_GAMMA,       .display = SSD1306, .EMC2101 = true, .emc_ideality_factor = 0x24, .emc_beta_compensation = 0x00, .TPS546 = true,                                                           .power_consumption_target = 22, },
    { .board_version = "800",  .family = FAMILY_GAMMA_TURBO, .display = SSD1306, .EMC2103 = true,                                                             .TPS546 = true,                                                           .power_consumption_target = 12, },
};

esp_err_t device_config_init(void * pvParameters);

#endif /* DEVICE_CONFIG_H_ */
