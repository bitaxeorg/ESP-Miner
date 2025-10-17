#ifndef INA260_H_
#define INA260_H_

#include "i2c_bitaxe.h"

#define INA260_I2CADDR_DEFAULT 0x40 ///< INA260 default i2c address
#define INA260_REG_CONFIG 0x00      ///< Configuration register
#define INA260_REG_CURRENT 0x01     ///< Current measurement register (signed) in mA
#define INA260_REG_BUSVOLTAGE 0x02  ///< Bus voltage measurement register in mV
#define INA260_REG_POWER 0x03       ///< Power calculation register in mW
#define INA260_REG_MASK_ENABLE 0x06 ///< Interrupt/Alert setting and checking register
#define INA260_REG_ALERT_LIMIT 0x07 ///< Alert limit value register
#define INA260_REG_MFG_UID 0xFE     ///< Manufacturer ID Register
#define INA260_REG_DIE_UID 0xFF     ///< Die ID and Revision Register

/**
 * @brief Mode options.
 *
 * Allowed values for setMode.
 */
typedef enum _mode
{
  INA260_MODE_SHUTDOWN = 0x00,   /**< SHUTDOWN: Minimize quiescient current and
                                  turn off current into the device inputs. Set
                                  another mode to exit shutown mode **/
  INA260_MODE_TRIGGERED = 0x03,  /**< TRIGGERED: Trigger a one-shot measurement
                                   of current and bus voltage. Set the TRIGGERED
                                   mode again to take a new measurement **/
  INA260_MODE_CONTINUOUS = 0x07, /**< CONTINUOUS: (Default) Continuously update
                                    the current, bus voltage and power
                                    registers with new measurements **/
} INA260_MeasurementMode;

/**
 * @brief Conversion Time options.
 *
 * Allowed values for setCurrentConversionTime and setVoltageConversionTime.
 */
typedef enum _conversion_time
{
  INA260_TIME_140_us,   ///< Measurement time: 140us
  INA260_TIME_204_us,   ///< Measurement time: 204us
  INA260_TIME_332_us,   ///< Measurement time: 332us
  INA260_TIME_558_us,   ///< Measurement time: 558us
  INA260_TIME_1_1_ms,   ///< Measurement time: 1.1ms (Default)
  INA260_TIME_2_116_ms, ///< Measurement time: 2.116ms
  INA260_TIME_4_156_ms, ///< Measurement time: 4.156ms
  INA260_TIME_8_244_ms, ///< Measurement time: 8.224ms
} INA260_ConversionTime;

/**
 * @brief Averaging Count options.
 *
 * Allowed values forsetAveragingCount.
 */
typedef enum _count
{
  INA260_COUNT_1,    ///< Window size: 1 sample (Default)
  INA260_COUNT_4,    ///< Window size: 4 samples
  INA260_COUNT_16,   ///< Window size: 16 samples
  INA260_COUNT_64,   ///< Window size: 64 samples
  INA260_COUNT_128,  ///< Window size: 128 samples
  INA260_COUNT_256,  ///< Window size: 256 samples
  INA260_COUNT_512,  ///< Window size: 512 samples
  INA260_COUNT_1024, ///< Window size: 1024 samples
} INA260_AveragingCount;

/**
 * @brief Alert trigger options.
 *
 * Allowed values for setAlertType.
 */
typedef enum _alert_type
{
  INA260_ALERT_CONVERSION_READY = 0x1, ///< Trigger on conversion ready
  INA260_ALERT_OVERPOWER = 0x2,        ///< Trigger on power over limit
  INA260_ALERT_UNDERVOLTAGE = 0x4,     ///< Trigger on bus voltage under limit
  INA260_ALERT_OVERVOLTAGE = 0x8,      ///< Trigger on bus voltage over limit
  INA260_ALERT_UNDERCURRENT = 0x10,    ///< Trigger on current under limit
  INA260_ALERT_OVERCURRENT = 0x20,     ///< Trigger on current over limit
  INA260_ALERT_NONE = 0x0,             ///< Do not trigger alert pin (Default)
} INA260_AlertType;

/**
 * @brief Alert pin polarity options.
 *
 * Allowed values for setAlertPolarity.
 */
typedef enum _alert_polarity
{
  INA260_ALERT_POLARITY_NORMAL = 0x0,   ///< Active high open-collector (Default)
  INA260_ALERT_POLARITY_INVERTED = 0x1, ///< Active low open-collector
} INA260_AlertPolarity;

/**
 * @brief Alert pin latch options.
 *
 * Allowed values for setAlertLatch.
 */
typedef enum _alert_latch
{
  INA260_ALERT_LATCH_ENABLED = 0x1,     /**< Alert will latch until Mask/Enable
                                           register is read **/
  INA260_ALERT_LATCH_TRANSPARENT = 0x0, /**< Alert will reset when fault is
                                           cleared **/
} INA260_AlertLatch;

esp_err_t INA260_init(void);
float INA260_read_current(void);
float INA260_read_voltage(void);
float INA260_read_power(void);

#endif /* INA260_H_ */
