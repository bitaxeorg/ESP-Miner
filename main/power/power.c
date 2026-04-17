#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"

void Power_get_output(GlobalState * GLOBAL_STATE, float * power_out, float * current_out)
{
    *current_out = 0.0f;
    *power_out   = 0.0f;

    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        *current_out = TPS546_get_iout() * 1000.0f;
        // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
        *power_out   = TPS546_get_vout() * (*current_out) / 1000.0f;
        *power_out  += GLOBAL_STATE->DEVICE_CONFIG.family.power_offset;  // Add offset for the rest of the Bitaxe power. TODO: this better.
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        *current_out = INA260_read_current();
        *power_out   = INA260_read_power() / 1000.0f;
    }
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
