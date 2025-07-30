#ifndef POWER_MANAGMENT_MODULE_H_
#define POWER_MANAGMENT_MODULE_H_
/**
 * @brief Structure representing a Power Management Module
 */
typedef struct
{
    /** @brief Fan percentage (0-100) */
    uint16_t fan_perc;
    /** @brief Fan speed in revolutions per minute (RPM) */
    uint16_t fan_rpm;
    /** @brief Array containing temperatures of up to 6 chips (in Celsius) */
    float chip_temp[6];
    /** @brief Average temperature of the chips (in Celsius) */
    float chip_temp_avg;
    /** @brief Voltage regulator temperature (VR temp) in Celsius */
    float vr_temp;
    /** @brief Input voltage value in Volts */
    float voltage;
    /** @brief Frequency value in Hertz */
    float frequency_value;
    /** @brief Power consumption in Watts */
    float power;
    /** @brief Current draw in Amperes */
    float current;
    /** @brief Core voltage (Vcore) in Volts */
    float core_voltage;
} PowerManagementModule;

extern PowerManagementModule POWER_MANAGEMENT_MODULE;

#endif