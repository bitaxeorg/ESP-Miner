#ifndef STATE_MODULE_H_
#define STATE_MODULE_H_

typedef struct
{
     // A flag indicating if a block has been found during mining.
    bool FOUND_BLOCK;

    // The current overheat mode setting for managing thermal conditions.
    uint16_t overheat_mode;

    // Status information about power faults encountered by the device.
    uint16_t power_fault;

    // Timestamp of the last clock synchronization event.
    uint32_t lastClockSync;

    // A flag indicating whether the screen is currently active.
    bool is_screen_active;

    // A flag indicating if a firmware update process is ongoing.
    bool is_firmware_update;

    // Filename for the firmware file being used or to be updated.
    char firmware_update_filename[20];

    // Status of the ongoing firmware update process.
    char firmware_update_status[20];

    // Current status string describing ASIC (Application-Specific Integrated Circuit) operation.
    char * asic_status;

    // A flag indicating if the ASIC is initialized and ready for operations.
    bool ASIC_initalized;

    // A flag indicating whether PSRAM (Pseudo-Static RAM) is available on the device.
    bool psram_is_available;
}StateModule;

extern StateModule STATE_MODULE;
#endif