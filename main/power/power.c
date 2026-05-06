#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"

void Power_get_output(GlobalState * GLOBAL_STATE, float * power_out, float * current_out)
{
    float cur_val = 0.0f;
    float pow_val = 0.0f;

    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        float iout = TPS546_get_iout();
        float vout = TPS546_get_vout();
        cur_val = iout * 1000.0f;
        // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
        pow_val   = vout * iout;
        pow_val  += GLOBAL_STATE->DEVICE_CONFIG.family.power_offset;  // Add offset for the rest of the Bitaxe power. TODO: this better.
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.INA260) {
        cur_val = INA260_read_current();
        pow_val = INA260_read_power() / 1000.0f;
    }

    *current_out = cur_val;
    *power_out   = pow_val;
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
