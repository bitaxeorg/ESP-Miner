#include "bzm/power.h"

#include <math.h>
#include <stddef.h>

#include "bzm/frequency.h"

bool bzm_power_runtime_voltage_is_allowed(float volts)
{
    return isfinite(volts) &&
           volts >= BZM_TPS546_MIN_VOUT_V &&
           volts <= BZM_TPS546_MAX_VOUT_V;
}

bool bzm_power_resolve_user_voltage(uint16_t millivolts, float *volts)
{
    if (volts == NULL) return false;
    const float requested_v = (float)millivolts / 1000.0f;
    if (!bzm_power_runtime_voltage_is_allowed(requested_v)) return false;
    *volts = requested_v;
    return true;
}

static void retain_first_error(bzm_status_t candidate, bzm_status_t *result)
{
    if (*result == BZM_OK && candidate != BZM_OK) *result = candidate;
}

static bzm_status_t power_down(const bzm_power_ops_t *ops, void *context)
{
    bzm_status_t result = BZM_OK;
    retain_first_error(ops->set_5v_enabled(context, false), &result);
    retain_first_error(ops->set_vout(context, 0.0f), &result);
    retain_first_error(ops->set_regulator_enabled(context, false), &result);
    return result;
}

static bool valid_ops(const bzm_power_ops_t *ops)
{
    return ops != NULL && ops->set_5v_enabled != NULL &&
           ops->set_regulator_enabled != NULL && ops->set_vout != NULL &&
           ops->validate_power != NULL && ops->delay_ms != NULL;
}

bzm_status_t bzm_power_set_rail_enabled(const bzm_power_ops_t *ops,
                                     void *context, bool enabled)
{
    if (!valid_ops(ops)) return BZM_ERR_INVALID_ARG;
    if (!enabled) return power_down(ops, context);

    bzm_status_t err = ops->set_5v_enabled(context, false);
    if (err == BZM_OK) {
        err = ops->set_regulator_enabled(context, true);
    }
    if (err == BZM_OK) {
        ops->delay_ms(context, 100);
        err = ops->set_vout(context, BZM_TPS546_FIXED_VOUT_V);
    }
    if (err == BZM_OK) {
        ops->delay_ms(context, 100);
        err = ops->validate_power(
            context, BZM_TPS546_FIXED_VOUT_V);
    }
    if (err != BZM_OK) {
        power_down(ops, context);
    }
    return err;
}

bzm_status_t bzm_power_set_runtime_voltage(const bzm_power_ops_t *ops,
                                        void *context, float volts)
{
    if (!valid_ops(ops) ||
        !bzm_power_runtime_voltage_is_allowed(volts)) {
        return BZM_ERR_INVALID_ARG;
    }

    bzm_status_t err = ops->set_vout(context, volts);
    if (err == BZM_OK) {
        ops->delay_ms(context, 100);
        err = ops->validate_power(context, volts);
    }
    if (err != BZM_OK) power_down(ops, context);
    return err;
}
