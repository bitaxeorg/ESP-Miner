#ifndef EMC2101_H_
#define EMC2101_H_

#include "i2c_bitaxe.h"

// TODO Make all these enums

#define EMC2101_BETA_11         0x00
#define EMC2101_BETA_18         0x01
#define EMC2101_BETA_25         0x02
#define EMC2101_BETA_33         0x03
#define EMC2101_BETA_43         0x04
#define EMC2101_BETA_100        0x05
#define EMC2101_BETA_233        0x06
#define EMC2101_BETA_DISABLED   0x07
#define EMC2101_BETA_AUTO       0x08 //default

#define EMC2101_FILTER_DISABLED 0x00 //default
#define EMC2101_FILTER_1        0x01
#define EMC2101_FILTER_2        0x02

#define EMC2101_DATARATE_1_16_HZ 0x00
#define EMC2101_DATARATE_1_8_HZ  0x01
#define EMC2101_DATARATE_1_4_HZ  0x02
#define EMC2101_DATARATE_1_2_HZ  0x03
#define EMC2101_DATARATE_1_HZ    0x04
#define EMC2101_DATARATE_2_HZ    0x05
#define EMC2101_DATARATE_4_HZ    0x06
#define EMC2101_DATARATE_8_HZ    0x07
#define EMC2101_DATARATE_16_HZ   0x08 //default
#define EMC2101_DATARATE_32_HZ   0x09

#define EMC2101_IDEALITY_0_9949	0x08
#define EMC2101_IDEALITY_0_9962	0x09
#define EMC2101_IDEALITY_0_9975	0x0A
#define EMC2101_IDEALITY_0_9988	0x0B
#define EMC2101_IDEALITY_1_0001	0x0C
#define EMC2101_IDEALITY_1_0014	0x0D
#define EMC2101_IDEALITY_1_0027	0x0E
#define EMC2101_IDEALITY_1_0040	0x0F
#define EMC2101_IDEALITY_1_0053	0x10
#define EMC2101_IDEALITY_1_0066	0x11
#define EMC2101_IDEALITY_1_0080	0x12 //default
#define EMC2101_IDEALITY_1_0093	0x13
#define EMC2101_IDEALITY_1_0106	0x14
#define EMC2101_IDEALITY_1_0119	0x15
#define EMC2101_IDEALITY_1_0133	0x16
#define EMC2101_IDEALITY_1_0146	0x17
#define EMC2101_IDEALITY_1_0159	0x18
#define EMC2101_IDEALITY_1_0172	0x19
#define EMC2101_IDEALITY_1_0185	0x1A
#define EMC2101_IDEALITY_1_0200	0x1B
#define EMC2101_IDEALITY_1_0212	0x1C
#define EMC2101_IDEALITY_1_0226	0x1D
#define EMC2101_IDEALITY_1_0239	0x1E
#define EMC2101_IDEALITY_1_0253	0x1F
#define EMC2101_IDEALITY_1_0267	0x20
#define EMC2101_IDEALITY_1_0280	0x21
#define EMC2101_IDEALITY_1_0293	0x22
#define EMC2101_IDEALITY_1_0306	0x23
#define EMC2101_IDEALITY_1_0319	0x24
#define EMC2101_IDEALITY_1_0332	0x25
#define EMC2101_IDEALITY_1_0345	0x26
#define EMC2101_IDEALITY_1_0358	0x27
#define EMC2101_IDEALITY_1_0371	0x28
#define EMC2101_IDEALITY_1_0384	0x29
#define EMC2101_IDEALITY_1_0397	0x2A
#define EMC2101_IDEALITY_1_0410	0x2B
#define EMC2101_IDEALITY_1_0423	0x2C
#define EMC2101_IDEALITY_1_0436	0x2D
#define EMC2101_IDEALITY_1_0449	0x2E
#define EMC2101_IDEALITY_1_0462	0x2F
#define EMC2101_IDEALITY_1_0475	0x30
#define EMC2101_IDEALITY_1_0488	0x31
#define EMC2101_IDEALITY_1_0501	0x32
#define EMC2101_IDEALITY_1_0514	0x33
#define EMC2101_IDEALITY_1_0527	0x34
#define EMC2101_IDEALITY_1_0540	0x35
#define EMC2101_IDEALITY_1_0553	0x36
#define EMC2101_IDEALITY_1_0566	0x37

#define EMC2101_I2CADDR_DEFAULT 0x4C ///< EMC2101 default i2c address
#define EMC2101_CHIP_ID 0x16         ///< EMC2101 default device id from part id
#define EMC2101_ALT_CHIP_ID 0x28     ///< EMC2101 alternate device id from part id
#define EMC2101_WHOAMI 0xFD          ///< Chip ID register

#define EMC2101_INTERNAL_TEMP 0x00     ///< The internal temperature register
#define EMC2101_EXTERNAL_TEMP_MSB 0x01 ///< high byte for the external temperature reading
#define EMC2101_EXTERNAL_TEMP_LSB 0x10 ///< low byte for the external temperature reading
#define EMC2101_TEMP_FAULT_OPEN_CIRCUIT 0x3F8
#define EMC2101_TEMP_FAULT_SHORT 0x3FF

#define EMC2101_STATUS 0x02          ///< Status register
#define EMC2101_REG_CONFIG 0x03      ///< configuration register
#define EMC2101_REG_DATA_RATE 0x04   ///< Data rate config
#define EMC2101_TEMP_FORCE 0x0C      ///< Temp force setting for LUT testing
#define EMC2101_IDEALITY_FACTOR 0x17      ///< Beta Compensation Register
#define EMC2101_BETA_COMPENSATION 0x18      ///< Beta Compensation Register
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

esp_err_t EMC2101_set_fan_speed(float);
uint16_t EMC2101_get_fan_speed(void);
esp_err_t EMC2101_init();
float EMC2101_get_external_temp(void);
float EMC2101_get_internal_temp(void);
esp_err_t EMC2101_set_ideality_factor(uint8_t);
esp_err_t EMC2101_set_beta_compensation(uint8_t);
#endif /* EMC2101_H_ */
