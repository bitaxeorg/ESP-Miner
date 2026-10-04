#ifndef BZM_TELEMETRY_H
#define BZM_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/chain.h"

/* Shared by board safety checks and the driver's temperature reader. Allows
 * bounded parser recovery; trip indications still require immediate shutdown. */
#define BZM_TELEMETRY_MAX_AGE_US UINT64_C(2000000)

typedef struct
{
    float temperature_min_c;
    float temperature_max_c;
    float ch0_min_mv;
    float ch0_max_mv;
    float ch1_min_mv;
    float ch1_max_mv;
    /* CH2 is not a third stack rail. It is the differential between the top
     * stack VSS and bottom stack VDD and should remain near zero. */
    float ch2_abs_max_mv;
    /* Maximum allowed difference between the bottom and top stack channels. */
    float max_stack_spread_mv;
} bzm_telemetry_bounds_t;

typedef struct
{
    uint8_t asic_id;
    uint64_t timestamp_us;
    bool received;

    uint16_t temperature_code;
    float temperature_c;
    bool thermal_enabled;
    bool thermal_validity;
    bool thermal_fault;
    bool thermal_trip;
    bool thermal_valid;

    uint16_t ch0_code;
    uint16_t ch1_code;
    uint16_t ch2_code;
    float ch0_mv;
    float ch1_mv;
    float ch2_mv;
    bool voltage_enabled;
    bool voltage_fault;
    bool voltage_trip;
    bool voltage_valid;

    /* TDM byte 7 bit 7 is the combined PLL0-and-PLL1 lock indication. Bits
     * 5 and 6 are reserved by the Intel Blockscale 1000 datasheet. */
    bool pll_locked;
    bool valid;
    bool trip;
} bzm_telemetry_sample_t;

typedef struct
{
    bzm_telemetry_sample_t samples[BZM_MAX_ASIC_COUNT];
} bzm_telemetry_store_t;

typedef struct
{
    uint8_t consecutive_excursions[BZM_MAX_ASIC_COUNT];
    uint64_t last_timestamp_us[BZM_MAX_ASIC_COUNT];
} bzm_ch2_confirmation_t;

typedef struct
{
    uint8_t consecutive_unlocks[BZM_MAX_ASIC_COUNT];
    uint64_t last_timestamp_us[BZM_MAX_ASIC_COUNT];
} bzm_pll_lock_confirmation_t;

typedef enum
{
    BZM_CH2_CONFIRMATION_GOOD = 0,
    BZM_CH2_CONFIRMATION_PENDING = 1,
    BZM_CH2_CONFIRMATION_CONTINUOUS = 2,
    BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE = 3,
    BZM_CH2_CONFIRMATION_INVALID = 4,
} bzm_ch2_confirmation_result_t;

bool bzm_telemetry_max_temperature(const bzm_telemetry_store_t *store,
                                   uint64_t now_us, uint64_t max_age_us,
                                   float *max_temperature_c);

const bzm_telemetry_sample_t * bzm_telemetry_store_get(const bzm_telemetry_store_t * store, uint8_t asic_id);

void bzm_ch2_confirmation_init(bzm_ch2_confirmation_t * confirmation);
bzm_ch2_confirmation_result_t bzm_ch2_confirmation_observe(bzm_ch2_confirmation_t * confirmation,
                                                           const bzm_telemetry_store_t * store,
                                                           const bzm_telemetry_bounds_t * bounds,
                                                           uint8_t required_consecutive_samples, uint8_t * culprit_asic_id,
                                                           uint8_t * observed_consecutive_samples);

/* Qualify only the unchecksummed combined PLL0/PLL1 telemetry bit. Initial
 * direct PLL register/lock validation remains a separate hard gate. */
void bzm_pll_lock_confirmation_init(bzm_pll_lock_confirmation_t * confirmation);
bzm_ch2_confirmation_result_t bzm_pll_lock_confirmation_observe(
    bzm_pll_lock_confirmation_t * confirmation, const bzm_telemetry_store_t * store, uint64_t now_us,
    uint64_t max_age_us, uint8_t required_consecutive_samples, uint8_t * culprit_asic_id,
    uint8_t * observed_consecutive_samples);

#endif // BZM_TELEMETRY_H
