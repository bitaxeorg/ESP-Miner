#ifndef EMC2101_H_
#define EMC2101_H_

#define EMC2101_I2CADDR_DEFAULT 0x4C ///< EMC2101 default i2c address
#define EMC2101_CHIP_ID 0x16         ///< EMC2101 default device id from part id
#define EMC2101_ALT_CHIP_ID 0x28     ///< EMC2101 alternate device id from part id
#define EMC2101_WHOAMI 0xFD          ///< Chip ID register

#define EMC2101_INTERNAL_TEMP 0x00     ///< The internal temperature register
#define EMC2101_EXTERNAL_TEMP_MSB 0x01 ///< high byte for the external temperature reading
#define EMC2101_EXTERNAL_TEMP_LSB 0x10 ///< low byte for the external temperature reading

#define EMC2101_STATUS 0x02          ///< Status register
#define EMC2101_REG_CONFIG 0x03      ///< configuration register
#define EMC2101_REG_DATA_RATE 0x04   ///< Data rate config
#define EMC2101_TEMP_FORCE 0x0C      ///< Temp force setting for LUT testing
#define EMC2101_TACH_LSB 0x46        ///< Tach RPM data low byte
#define EMC2101_TACH_MSB 0x47        ///< Tach RPM data high byte
#define EMC2101_TACH_LIMIT_LSB 0x48  ///< Tach low-speed setting low byte. INVERSE OF THE SPEED
#define EMC2101_TACH_LIMIT_MSB 0x49  ///< Tach low-speed setting high byte. INVERSE OF THE SPEED
#define EMC2101_FAN_CONFIG 0x4A      ///< General fan config register
#define EMC2101_FAN_SPINUP 0x4B      ///< Fan spinup behavior settings
#define EMC2101_REG_FAN_SETTING 0x4C ///< Fan speed for non-LUT settings, as a % PWM duty cycle
#define EMC2101_PWM_FREQ 0x4D        ///< PWM frequency setting
#define EMC2101_PWM_DIV 0x4E         ///< PWM frequency divisor
#define EMC2101_LUT_HYSTERESIS 0x4F  ///< The hysteresis value for LUT lookups when temp is decreasing

#define EMC2101_LUT_START 0x50 ///< The first temp threshold register

#define EMC2101_TEMP_FILTER 0xBF ///< The external temperature sensor filtering behavior
#define EMC2101_REG_PARTID 0xFD  ///< 0x16
#define EMC2101_REG_MFGID 0xFE   ///< 0xFF16

#define MAX_LUT_SPEED 0x3F ///< 6-bit value
#define MAX_LUT_TEMP 0x7F  ///<  7-bit

#define EMC2101_I2C_ADDR 0x4C             ///< The default I2C address
#define EMC2101_FAN_RPM_NUMERATOR 5400000 ///< Conversion unit to convert LSBs to fan RPM
#define _TEMP_LSB 0.125                   ///< single bit value for internal temperature readings

#define FAN_LOOKUP_TABLE_T1 0x50
#define FAN_LOOKUP_TABLE_S1 0x51

#define FAN_LOOKUP_TABLE_T2 0x52
#define FAN_LOOKUP_TABLE_S2 0x53

#define FAN_LOOKUP_TABLE_T3 0x54
#define FAN_LOOKUP_TABLE_S3 0x55

#define FAN_LOOKUP_TABLE_T4 0x56
#define FAN_LOOKUP_TABLE_S4 0x57

#define FAN_LOOKUP_TABLE_T5 0x58
#define FAN_LOOKUP_TABLE_S5 0x59

#define FAN_LOOKUP_TABLE_T6 0x5A
#define FAN_LOOKUP_TABLE_S6 0x5B

#define FAN_LOOKUP_TABLE_T7 0x5C
#define FAN_LOOKUP_TABLE_S7 0x5D

#define FAN_LOOKUP_TABLE_T8 0x5E
#define FAN_LOOKUP_TABLE_S8 0x5F

/**
 * @brief
 *
 * Allowed values for `setDataRate`.
 */
typedef enum
{
    EMC2101_RATE_1_16_HZ, ///< 1_16_HZ
    EMC2101_RATE_1_8_HZ,  ///< 1_8_HZ
    EMC2101_RATE_1_4_HZ,  ///< 1_4_HZ
    EMC2101_RATE_1_2_HZ,  ///< 1_2_HZ
    EMC2101_RATE_1_HZ,    ///< 1_HZ
    EMC2101_RATE_2_HZ,    ///< 2_HZ
    EMC2101_RATE_4_HZ,    ///< 4_HZ
    EMC2101_RATE_8_HZ,    ///< 8_HZ
    EMC2101_RATE_16_HZ,   ///< 16_HZ
    EMC2101_RATE_32_HZ,   ///< 32_HZ
} emc2101_rate_t;

void EMC2101_set_fan_speed(float);
// void EMC2101_read(void);
uint16_t EMC2101_get_fan_speed(void);
void EMC2101_init(bool);
float EMC2101_get_external_temp(void);
uint8_t EMC2101_get_internal_temp(void);
#endif /* EMC2101_H_ */