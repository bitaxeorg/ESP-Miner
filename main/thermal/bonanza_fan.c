#include "thermal.h"
#include "bzm/control.h"

static uint16_t bonanza_fan_rpm(void)
{
    uint16_t rpm = 0;
    (void)BZM_control_get_fan_rpm(&rpm);
    return rpm;
}

const FanDriver BONANZA_FAN_DRIVER = {
    .set_percent = BZM_control_set_fan_percent,
    .get_rpm = bonanza_fan_rpm,
};
