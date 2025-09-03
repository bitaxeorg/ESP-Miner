#ifndef ASIC_MANAGEMENT_TASK_H_
#define ASIC_MANAGEMENT_TASK_H_

#include "power_management_task.h"

void ASIC_MANAGEMENT_init_frequency(PowerManagementModule * power_management);

void ASIC_MANAGEMENT_task(void * pvParameters);

#endif /* ASIC_MANAGEMENT_TASK_H_ */
