#include "bzm/controls.h"
#include <stddef.h>

static bzm_control_ops_t control;
static void *control_context;

bool bzm_control_bind(const bzm_control_ops_t *ops, void *context)
{
    if (ops == NULL || ops->init == NULL || ops->get_safety_status == NULL ||
        ops->clear_safety_fault == NULL || ops->safe_off == NULL ||
        ops->set_5v_enabled == NULL || ops->set_asic_reset == NULL ||
        ops->set_fan_percent == NULL || ops->get_fan_rpm == NULL ||
        ops->set_core_enabled == NULL) return false;
    control = *ops;
    control_context = context;
    return true;
}

bool bzm_control_status_allows_fault_clear(const bzm_control_status_t *status)
{
    return status != NULL && status->valid && !status->core_enabled &&
           !status->five_volt_enabled && status->asic_reset_asserted &&
           status->fan_full && status->fan_percent == 100 && !status->trip_input_asserted;
}

bzm_status_t bzm_control_init(const char * board_version)
{
    return control.init ? control.init(control_context, board_version) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_get_safety_status(bzm_control_status_t * status)
{
    if (status == NULL) return BZM_ERR_INVALID_ARG;
    *status = (bzm_control_status_t){.fault = BZM_CONTROL_FAULT_STATUS_INVALID};
    return control.get_safety_status ? control.get_safety_status(control_context, status) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_clear_safety_fault(bzm_control_status_t * status)
{
    if (status == NULL) return BZM_ERR_INVALID_ARG;
    *status = (bzm_control_status_t){.fault = BZM_CONTROL_FAULT_STATUS_INVALID};
    return control.clear_safety_fault ? control.clear_safety_fault(control_context, status) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_safe_off(bzm_control_status_t * status)
{
    if (status == NULL) return BZM_ERR_INVALID_ARG;
    *status = (bzm_control_status_t){.fault = BZM_CONTROL_FAULT_STATUS_INVALID};
    return control.safe_off ? control.safe_off(control_context, status) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_set_5v_enabled(bool enabled)
{
    return control.set_5v_enabled ? control.set_5v_enabled(control_context, enabled) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_set_asic_reset(bool high)
{
    return control.set_asic_reset ? control.set_asic_reset(control_context, high) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_set_fan_percent(float percent)
{
    return control.set_fan_percent ? control.set_fan_percent(control_context, percent) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_get_fan_rpm(uint16_t * rpm)
{
    return control.get_fan_rpm ? control.get_fan_rpm(control_context, rpm) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_set_core_enabled(bool enabled)
{
    return control.set_core_enabled ? control.set_core_enabled(control_context, enabled) : BZM_ERR_INVALID_STATE;
}

bzm_status_t bzm_control_pulse_asic_reset(void)
{
    bzm_status_t status = bzm_control_set_asic_reset(false);
    if (status != BZM_OK) return status;
    bzm_sleep_ms(100);
    status = bzm_control_set_asic_reset(true);
    if (status != BZM_OK) return status;
    bzm_sleep_ms(100);
    return BZM_OK;
}
