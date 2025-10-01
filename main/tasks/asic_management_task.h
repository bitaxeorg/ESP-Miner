#ifndef ASIC_MANAGEMENT_TASK_H_
#define ASIC_MANAGEMENT_TASK_H_

void ASIC_MANAGEMENT_init_frequency(void * pvParameters);
bool ASIC_MANAGEMENT_adjust_voltage(void * pvParameters);
bool ASIC_MANAGEMENT_adjust_frequency(void * pvParameters);
void ASIC_MANAGEMENT_task(void * pvParameters);

#endif /* ASIC_MANAGEMENT_TASK_H_ */
