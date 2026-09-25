#ifndef BONANZA_TPS546_H_
#define BONANZA_TPS546_H_

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include <stdbool.h>

typedef struct GlobalState GlobalState;

#define BONANZA_TPS546_I2CADDR         0x24  // TPS546 i2c address
#define BONANZA_TPS546_I2CADDR_ALERT   0x0C  // TPS546 SMBus Alert address
#define BONANZA_TPS546_MANUFACTURER_ID 0xFE  // Manufacturer ID
#define BONANZA_TPS546_REVISION        0xFF  // Chip revision

#define OPERATION_OFF 0x00
#define OPERATION_ON  0x80

typedef struct {
  uint16_t status_word;
  uint8_t  operation;
  float    read_vout, read_vin, read_iout;
  int      read_temp1;
  float    vout_command;
  uint16_t vout_command_raw;
  bool     vout_command_matches_active_config;
} BONANZA_TPS546_StatusSnapshot;

/* PMBUS_ON_OFF_CONFIG initialization values */
#define ON_OFF_CONFIG_PU        0x10 // Act on CONTROL. (01h) OPERATION command to start/stop power conversion, or both.
#define ON_OFF_CONFIG_CMD       0x08 // Act on (01h) OPERATION Command (and CONTROL pin if configured by CP) to start/stop power conversion.
#define ON_OFF_CONFIG_CP        0x04 // Act on CONTROL pin (and (01h) OPERATION Command if configured by bit [3]) to start/stop power conversion.
#define ON_OFF_CONFIG_POLARITY  0x02 // CONTROL pin has active high polarity.
#define ON_OFF_CONFIG_DELAY     0x01 // When power conversion is commanded OFF by the CONTROL pin (must be configured to respect the CONTROL pin as above), stop power conversion immediately.

//// STATUS_WORD Offsets
#define BONANZA_TPS546_STATUS_VOUT    0x8000 //bit 15
#define BONANZA_TPS546_STATUS_IOUT    0x4000
#define BONANZA_TPS546_STATUS_INPUT   0x2000
#define BONANZA_TPS546_STATUS_MFR     0x1000
#define BONANZA_TPS546_STATUS_PGOOD   0x0800
#define BONANZA_TPS546_STATUS_OTHER   0x0200

#define BONANZA_TPS546_STATUS_BUSY    0x0080
#define BONANZA_TPS546_STATUS_OFF     0x0040
#define BONANZA_TPS546_STATUS_VOUT_OV 0x0020
#define BONANZA_TPS546_STATUS_IOUT_OC 0x0010
#define BONANZA_TPS546_STATUS_VIN_UV  0x0008
#define BONANZA_TPS546_STATUS_TEMP    0x0004
#define BONANZA_TPS546_STATUS_CML     0x0002
#define BONANZA_TPS546_STATUS_NONE    0x0001

/* STATUS_VOUT OFFSETS */
#define BONANZA_TPS546_STATUS_VOUT_OVF     0x80 //bit 7 - Latched flag indicating a VOUT OV fault has occurred.
#define BONANZA_TPS546_STATUS_VOUT_OVW     0x40 //bit 6 - Latched flag indicating a VOUT OV warn has occurred.
#define BONANZA_TPS546_STATUS_VOUT_UVW     0x20 //bit 5 - Latched flag indicating a VOUT UV warn has occurred.
#define BONANZA_TPS546_STATUS_VOUT_UVF     0x10 //bit 4 - Latched flag indicating a VOUT UV fault has occurred.
#define BONANZA_TPS546_STATUS_VOUT_MIN_MAX 0x08 //bit 3 - Latched flag indicating a VOUT_MIN_MAX has occurred.
#define BONANZA_TPS546_STATUS_VOUT_TON_MAX 0x04 //bit 2 - Latched flag indicating a TON_MAX has occurred.

/* STATUS_IOUT OFFSETS */
#define BONANZA_TPS546_STATUS_IOUT_OCF     0x80 //bit 7 - Latched flag indicating IOUT OC fault has occurred.
#define BONANZA_TPS546_STATUS_IOUT_OCW     0x20 //bit 5 - Latched flag indicating IOUT OC warn has occurred.

/* STATUS_INPUT OFFSETS */
#define BONANZA_TPS546_STATUS_VIN_OVF      0x80 //bit 7 - Latched flag indicating PVIN OV fault has occurred.
#define BONANZA_TPS546_STATUS_VIN_UVW      0x20 //bit 5 - Latched flag indicating PVIN UV warn has occurred.
#define BONANZA_TPS546_STATUS_VIN_LOW_VIN  0x08 //bit 3 - LIVE (unlatched) status bit. PVIN is OFF.

/* STATUS_TEMPERATURE OFFSETS */
#define BONANZA_TPS546_STATUS_TEMP_OTF     0x80 //bit 7 - Latched flag indicating OT fault has occurred.
#define BONANZA_TPS546_STATUS_TEMP_OTW     0x40 //bit 6 - Latched flag indicating OT warn has occurred

/* STATUS_CML OFFSETS */
#define BONANZA_TPS546_STATUS_CML_IVC     0x80 //bit 7 - Latched flag indicating an invalid or unsupported command was received.
#define BONANZA_TPS546_STATUS_CML_IVD     0x40 //bit 6 - Latched flag indicating an invalid or unsupported data was received.
#define BONANZA_TPS546_STATUS_CML_PEC     0x20 //bit 5 - Latched flag indicating a packet error check has failed.
#define BONANZA_TPS546_STATUS_CML_MEM     0x10 //bit 4 - Latched flag indicating a memory error was detected.
#define BONANZA_TPS546_STATUS_CML_PROC    0x08 //bit 3 - Latched flag indicating a logic core error was detected.
#define BONANZA_TPS546_STATUS_CML_COMM    0x02 //bit 1 - Latched flag indicating communication error detected.

/* STATUS_OTHER */
#define BONANZA_TPS546_STATUS_OTHER_FIRST       0x01 //bit 0 - Latched flag indicating that this device was the first to assert SMBALERT.

/* STATUS_MFG */
#define BONANZA_TPS546_STATUS_MFR_POR     0x80 //bit 7 - A Power-On Reset Fault has been detected.
#define BONANZA_TPS546_STATUS_MFR_SELF    0x40 //bit 6 - Power-On Self-Check is in progress. One or more BCX slaves have not responded.
#define BONANZA_TPS546_STATUS_MFR_RESET   0x08 //bit 3 - A RESET_VOUT event has occurred.
#define BONANZA_TPS546_STATUS_MFR_BCX     0x04 //bit 2 - A BCX fault event has occurred.
#define BONANZA_TPS546_STATUS_MFR_SYNC    0x02 //bit 1 - A SYNC fault has been detected.

/* public functions */
esp_err_t BONANZA_TPS546_init(void);

int BONANZA_TPS546_get_temperature(void);
float BONANZA_TPS546_get_vin(void);
float BONANZA_TPS546_get_iout(void);
float BONANZA_TPS546_get_vout(void);
esp_err_t BONANZA_TPS546_set_vout(float volts);
esp_err_t BONANZA_TPS546_check_phase_currents(uint8_t phase_count, float minimum_current_a);
uint8_t BONANZA_TPS546_get_phase_count(void);

esp_err_t BONANZA_TPS546_check_status(GlobalState * GLOBAL_STATE);

const char* BONANZA_TPS546_get_error_message(void); //Get the current TPS error message
void BONANZA_TPS546_log_snapshot(const BONANZA_TPS546_StatusSnapshot *s);
esp_err_t BONANZA_TPS546_snapshot_status(BONANZA_TPS546_StatusSnapshot *s);

/**
 * Read back the Bonanza power topology, feedback and hard protection settings.
 * On failure, detail identifies the first register that could not be read or
 * whose encoded value did not match. Success leaves detail empty.
 */
esp_err_t BONANZA_TPS546_check_protection(char *detail, size_t detail_len);

#endif /* BONANZA_TPS546_H_ */
