#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"

float Power_get_current(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_iout() * 1000.0;
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        return INA260_read_current();
    }

    return 0.0;
}

float Power_get_power(GlobalState * GLOBAL_STATE)
{
    float power = 0.0;
    float current = 0.0;

    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        current = TPS546_get_iout() * 1000.0;
        // calculate regulator power (in milliwatts)
        power = TPS546_get_vout() * current / 1000.0;
        // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
        power += GLOBAL_STATE->DEVICE_CONFIG.family.power_offset; // Add offset for the rest of the Bitaxe power. TODO: this better.
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        power = INA260_read_power() / 1000.0;
    }    

    return power;
}

float Power_get_input_voltage(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_vin() * 1000.0;
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        return INA260_read_voltage();
    }
    
    return 0.0;
}

float Power_get_vreg_temp(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        return TPS546_get_temperature();
    }

    return 0.0;
}
