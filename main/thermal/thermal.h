#ifndef THERMAL_H
#define THERMAL_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

// Physically plausible temperature envelope shared by the VR and ASIC
// readings. A value outside it (e.g. a corrupted PMBus read yielding 16384 C)
// is a sensor fault, not heat, and must never be acted upon.
#ifndef TEMP_PLAUSIBLE_MIN
#define TEMP_PLAUSIBLE_MIN (-40.0f)
#define TEMP_PLAUSIBLE_MAX 150.0f
#endif

// Returns true when the temperature reading can be trusted. Shared between
// power_management_task and fan_controller_task so the check is not duplicated.
static inline bool temp_is_plausible(float temp_c)
{
    return temp_c >= TEMP_PLAUSIBLE_MIN && temp_c <= TEMP_PLAUSIBLE_MAX;
}

typedef struct GlobalState GlobalState;
typedef struct DeviceConfig DeviceConfig;

esp_err_t Thermal_init(DeviceConfig * DEVICE_CONFIG);
esp_err_t Thermal_set_fan_percent(DeviceConfig * DEVICE_CONFIG, float percent);
uint16_t Thermal_get_fan_speed(DeviceConfig * DEVICE_CONFIG);
uint16_t Thermal_get_fan2_speed(DeviceConfig * DEVICE_CONFIG);

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE);
float Thermal_get_chip_temp2(GlobalState * GLOBAL_STATE);

#endif // THERMAL_H
