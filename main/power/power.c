#include "global_state.h"
#include "device_config.h"
#include "power.h"
#include "TPS546.h"
#include "INA260.h"

void Power_get_output(GlobalState * GLOBAL_STATE, float * power_out, float * current_out)
{
    float cur_val = 0.0f;
    float pow_val = 0.0f;

    if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
        float iout = TPS546_get_iout();
        float vout = TPS546_get_vout();
        cur_val = iout * 1000.0f;

        /* Prefer real input power at the buck: VIN × IIN from TPS546 PMBus telemetry.
         * This is wall-side power into the regulator (includes buck efficiency loss).
         * TPS546D24A exposes READ_VIN (0x88); READ_IIN (0x89) may or may not be
         * supported by the specific die revision — if the read fails, fall back to
         * estimating input power from output power / efficiency. */
        float vin = TPS546_get_vin();
        float iin = TPS546_get_iin();

        if (vin > 0.0f && iin > 0.0f) {
            pow_val = vin * iin;                 /* real input power, W */
        } else {
            /* Fallback: estimate input power from output power divided by estimated
             * buck efficiency. TPS546 buck (12V -> 1.2V) runs ~85-92% efficient;
             * use 0.90 as conservative nominal. Documented estimate, not measured. */
            #define TPS546_EFFICIENCY_ESTIMATE 0.90f
            pow_val = (vout * iout) / TPS546_EFFICIENCY_ESTIMATE;
        }

        /* Add fixed board overhead: ESP32 + fan + other rails. This is the same
         * power_offset from family config (default 5 W). The old code reported only
         * Vout×Iout + power_offset, which under-reported because it ignored buck
         * efficiency loss (now captured by VIN×IIN or the efficiency division). */
        pow_val += GLOBAL_STATE->DEVICE_CONFIG.family.power_offset;
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
