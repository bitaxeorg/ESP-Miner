#ifndef TPS546_H_
#define TPS546_H_

#include <stdint.h>
#include <esp_err.h>
#include <stdbool.h>

#include "global_state.h"

#define TPS546_I2CADDR         0x24  // TPS546 i2c address
#define TPS546_I2CADDR_ALERT   0x0C  // TPS546 SMBus Alert address
#define TPS546_MANUFACTURER_ID 0xFE  // Manufacturer ID
#define TPS546_REVISION        0xFF  // Chip revision

/*-------------------------*/
/* These are the inital values for the voltage regulator configuration */
/* when the config revision stored in the TPS546 doesn't match, these values are used */


//#define TPS546_INIT_ON_OFF_CONFIG 0x18 /* use ON_OFF command to control power */
#define OPERATION_OFF 0x00
#define OPERATION_ON  0x80

#define TPS546_INIT_PHASE_SINGLE 0x00  /* Single-phase (Single TPS) */
#define TPS546_INIT_PHASE_MULTI   0xFF  /* Multi-phase stack (Multi TPS) */

#define TPS546_INIT_FREQUENCY 650  /* KHz */


typedef struct {
  uint16_t status_word;
  uint8_t  st_vout, st_input, st_iout, st_temp, st_cml, st_mfr, st_other;
  uint8_t  operation, on_off_config;
  float    read_vout, read_vin, read_iout;
  int      read_temp1;
  float    vout_command;
} TPS546_StatusSnapshot;

typedef struct
{
  /* Phase readout configuration */
  uint8_t TPS546_INIT_PHASE; /* phase register configuration */
  /* vin voltage */
  float TPS546_INIT_VIN_ON;  /* V */
  float TPS546_INIT_VIN_OFF; /* V */
  float TPS546_INIT_VIN_UV_WARN_LIMIT; /* V */
  float TPS546_INIT_VIN_OV_FAULT_LIMIT; /* V */
  /* vout voltage */
  float TPS546_INIT_SCALE_LOOP; /* Voltage Scale factor */
  float TPS546_INIT_VOUT_MIN; /* V */
  float TPS546_INIT_VOUT_MAX; /* V */
  float TPS546_INIT_VOUT_COMMAND;  /* V absolute value */
  /* iout current */
  float TPS546_INIT_IOUT_OC_WARN_LIMIT; /* A */
  float TPS546_INIT_IOUT_OC_FAULT_LIMIT; /* A */

  
  uint16_t TPS546_INIT_STACK_CONFIG; /* Stack configuration */
  uint8_t TPS546_INIT_SYNC_CONFIG; /* Sync configuration */
  uint8_t TPS546_INIT_COMPENSATION_CONFIG[5];
  
} TPS546_CONFIG;

/* vin voltage */
// #define TPS546_INIT_VIN_ON  11.0  /* V */
// #define TPS546_INIT_VIN_OFF 10.5  /* V */
// #define TPS546_INIT_VIN_UV_WARN_LIMIT 14.0 /* V */
// #define TPS546_INIT_VIN_OV_FAULT_LIMIT 15.0 /* V */

//VIN_OV_FAULT_RESPONSE pg98
//0xB7 -> 1011 0111
//10 -> Immediate Shutdown. Shut down and restart according to VIN_OV_RETRY.
//110 -> After shutting down, wait one HICCUP period, and attempt to restart up to 6 times. After 6 failed restart attempts, do not attempt to restart (latch off).
//111 -> Shutdown delay of seven PWM_CLK, HICCUP equal to 7 times TON_RISE
#define TPS546_INIT_VIN_OV_FAULT_RESPONSE 0xB7

  /* vout voltage */
//#define TPS546_INIT_SCALE_LOOP 0.25  /* Voltage Scale factor */
//#define TPS546_INIT_VOUT_MAX 3 /* V */
#define TPS546_INIT_VOUT_OV_FAULT_LIMIT 1.25 /* %/100 above VOUT_COMMAND */
#define TPS546_INIT_VOUT_OV_WARN_LIMIT  1.16 /* %/100 above VOUT_COMMAND */
#define TPS546_INIT_VOUT_MARGIN_HIGH 1.1 /* %/100 above VOUT */
//#define TPS546_INIT_VOUT_COMMAND 1.2  /* V absolute value */
#define TPS546_INIT_VOUT_MARGIN_LOW 0.90 /* %/100 below VOUT */
#define TPS546_INIT_VOUT_UV_WARN_LIMIT 0.90  /* %/100 below VOUT_COMMAND */
#define TPS546_INIT_VOUT_UV_FAULT_LIMIT 0.75 /* %/100 below VOUT_COMMAND */
//#define TPS546_INIT_VOUT_MIN 1 /* v */

  /* iout current */
// #define TPS546_INIT_IOUT_OC_WARN_LIMIT  50.00 /* A */
// #define TPS546_INIT_IOUT_OC_FAULT_LIMIT 55.00 /* A */

//IOUT_OC_FAULT_RESPONSE - pg91
//0xC0 -> 1100 0000
//11 -> Shutdown Immediately
//000 -> Do not attempt to restart (latch off).
//000 -> Shutdown delay of one PWM_CLK, HICCUP equal to TON_RISE
#define TPS546_INIT_IOUT_OC_FAULT_RESPONSE 0xC0  /* shut down, no retries */

  /* temperature */
// It is better to set the temperature warn limit for TPS546 more higher than Ultra 
#define TPS546_INIT_OT_WARN_LIMIT  105 /* degrees C */
#define TPS546_INIT_OT_FAULT_LIMIT 145 /* degrees C */

//OT_FAULT_RESPONSE - pg94
//0xFF -> 1111 1111
//11 -> Shutdown until Temperature is below OT_WARN_LIMIT, then restart according to OT_RETRY*.
//111 -> After shutting down, wait one HICCUP period, and attempt to restart indefinitely, until commanded OFF or a successful start-up occurs.
//111 -> Shutdown delay of 7 ms, HICCUP equal to 4 times TON_RISE
#define TPS546_INIT_OT_FAULT_RESPONSE 0xFF /* wait for cooling, and retry */

  /* timing */
#define TPS546_INIT_TON_DELAY 0
#define TPS546_INIT_TON_RISE 3
#define TPS546_INIT_TON_MAX_FAULT_LIMIT 0
#define TPS546_INIT_TON_MAX_FAULT_RESPONSE 0x3B
#define TPS546_INIT_TOFF_DELAY 0
#define TPS546_INIT_TOFF_FALL 0

#define INIT_STACK_CONFIG 0x0001 //One-Slave, 2-phase
#define INIT_SYNC_CONFIG 0x00D0 //Enable Auto Detect SYNC
#define INIT_PIN_DETECT_OVERRIDE 0xFFFF //use pin values

/*-------------------------*/

/* PMBUS_ON_OFF_CONFIG initialization values */
#define ON_OFF_CONFIG_PU        0x10 // Act on CONTROL. (01h) OPERATION command to start/stop power conversion, or both.
#define ON_OFF_CONFIG_CMD       0x08 // Act on (01h) OPERATION Command (and CONTROL pin if configured by CP) to start/stop power conversion.
#define ON_OFF_CONFIG_CP        0x04 // Act on CONTROL pin (and (01h) OPERATION Command if configured by bit [3]) to start/stop power conversion.
#define ON_OFF_CONFIG_POLARITY  0x02 // CONTROL pin has active high polarity.
#define ON_OFF_CONFIG_DELAY     0x01 // When power conversion is commanded OFF by the CONTROL pin (must be configured to respect the CONTROL pin as above), stop power conversion immediately.

//// STATUS_WORD Offsets
#define TPS546_STATUS_VOUT    0x8000 //bit 15
#define TPS546_STATUS_IOUT    0x4000
#define TPS546_STATUS_INPUT   0x2000
#define TPS546_STATUS_MFR     0x1000
#define TPS546_STATUS_PGOOD   0x0800
#define TPS546_STATUS_OTHER   0x0200

#define TPS546_STATUS_BUSY    0x0080
#define TPS546_STATUS_OFF     0x0040
#define TPS546_STATUS_VOUT_OV 0x0020
#define TPS546_STATUS_IOUT_OC 0x0010
#define TPS546_STATUS_VIN_UV  0x0008
#define TPS546_STATUS_TEMP    0x0004
#define TPS546_STATUS_CML     0x0002
#define TPS546_STATUS_NONE    0x0001

/* STATUS_VOUT OFFSETS */
#define TPS546_STATUS_VOUT_OVF     0x80 //bit 7 - Latched flag indicating a VOUT OV fault has occurred.
#define TPS546_STATUS_VOUT_OVW     0x40 //bit 6 - Latched flag indicating a VOUT OV warn has occurred.
#define TPS546_STATUS_VOUT_UVW     0x20 //bit 5 - Latched flag indicating a VOUT UV warn has occurred.
#define TPS546_STATUS_VOUT_UVF     0x10 //bit 4 - Latched flag indicating a VOUT UV fault has occurred.
#define TPS546_STATUS_VOUT_MIN_MAX 0x08 //bit 3 - Latched flag indicating a VOUT_MIN_MAX has occurred.
#define TPS546_STATUS_VOUT_TON_MAX 0x04 //bit 2 - Latched flag indicating a TON_MAX has occurred.

/* STATUS_IOUT OFFSETS */
#define TPS546_STATUS_IOUT_OCF     0x80 //bit 7 - Latched flag indicating IOUT OC fault has occurred.
#define TPS546_STATUS_IOUT_OCW     0x20 //bit 5 - Latched flag indicating IOUT OC warn has occurred.

/* STATUS_INPUT OFFSETS */
#define TPS546_STATUS_VIN_OVF      0x80 //bit 7 - Latched flag indicating PVIN OV fault has occurred.
#define TPS546_STATUS_VIN_UVW      0x20 //bit 5 - Latched flag indicating PVIN UV warn has occurred.
#define TPS546_STATUS_VIN_LOW_VIN  0x08 //bit 3 - LIVE (unlatched) status bit. PVIN is OFF.

/* STATUS_TEMPERATURE OFFSETS */
#define TPS546_STATUS_TEMP_OTF     0x80 //bit 7 - Latched flag indicating OT fault has occurred.
#define TPS546_STATUS_TEMP_OTW     0x40 //bit 6 - Latched flag indicating OT warn has occurred

/* STATUS_CML OFFSETS */
#define TPS546_STATUS_CML_IVC     0x80 //bit 7 - Latched flag indicating an invalid or unsupported command was received.
#define TPS546_STATUS_CML_IVD     0x40 //bit 6 - Latched flag indicating an invalid or unsupported data was received.
#define TPS546_STATUS_CML_PEC     0x20 //bit 5 - Latched flag indicating a packet error check has failed.
#define TPS546_STATUS_CML_MEM     0x10 //bit 4 - Latched flag indicating a memory error was detected.
#define TPS546_STATUS_CML_PROC    0x08 //bit 3 - Latched flag indicating a logic core error was detected.
#define TPS546_STATUS_CML_COMM    0x02 //bit 1 - Latched flag indicating communication error detected.

/* STATUS_OTHER */
#define TPS546_STATUS_OTHER_FIRST       0x01 //bit 0 - Latched flag indicating that this device was the first to assert SMBALERT.

/* STATUS_MFG */
#define TPS546_STATUS_MFR_POR     0x80 //bit 7 - A Power-On Reset Fault has been detected.
#define TPS546_STATUS_MFR_SELF    0x40 //bit 6 - Power-On Self-Check is in progress. One or more BCX slaves have not responded.
#define TPS546_STATUS_MFR_RESET   0x08 //bit 3 - A RESET_VOUT event has occurred.
#define TPS546_STATUS_MFR_BCX     0x04 //bit 2 - A BCX fault event has occurred.
#define TPS546_STATUS_MFR_SYNC    0x02 //bit 1 - A SYNC fault has been detected.


/* public functions */
esp_err_t TPS546_init(TPS546_CONFIG config);

void TPS546_read_mfr_info(uint8_t *);
void TPS546_write_entire_config(void);
int TPS546_get_frequency(void);
void TPS546_set_frequency(int);
int TPS546_get_temperature(void);
float TPS546_get_vin(void);
float TPS546_get_iout(void);
float TPS546_get_vout(void);
esp_err_t TPS546_set_vout(float volts);
void TPS546_show_voltage_settings(void);
void TPS546_print_status(void);

esp_err_t TPS546_check_status(GlobalState * GLOBAL_STATE);
esp_err_t TPS546_clear_faults(void);

const char* TPS546_get_error_message(void); //Get the current TPS error message
void TPS546_log_snapshot(const TPS546_StatusSnapshot *s);
esp_err_t TPS546_snapshot_status(TPS546_StatusSnapshot *s);

#endif /* TPS546_H_ */
