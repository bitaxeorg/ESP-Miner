#ifndef THERMAL_H
#define THERMAL_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

// Limite fisicamente plausivel para leitura de temperatura (VR e ASIC).
// Valor fora dele (leitura PMBus corrompida, p. ex. 16384 C) e falha de
// sensor, nao calor, e nunca deve ser acionado.
#ifndef TEMP_PLAUSIBLE_MIN
#define TEMP_PLAUSIBLE_MIN (-40.0f)
#define TEMP_PLAUSIBLE_MAX 150.0f
#endif

// Devolve true se a leitura de temperatura eh confiavel. Compartilhado entre
// power_management_task e fan_controller_task para nao duplicar a logica.
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
