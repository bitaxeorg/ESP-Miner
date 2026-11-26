#ifndef BZM_RESULT_ADAPTER_H
#define BZM_RESULT_ADAPTER_H
#include "asic_common.h"
#include "bzm/runtime_result.h"
bool bzm_result_to_task(const bzm_result_t *share, task_result *result);
#endif
