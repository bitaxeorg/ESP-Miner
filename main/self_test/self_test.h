#ifndef SELF_TEST_H_
#define SELF_TEST_H_

#include "esp_err.h"

#define SELF_TEST_CORE_VOLTAGE_MV 1200

esp_err_t self_test_init(void * pvParameters);
void self_test_task(void * pvParameters);
void self_test_show_message(void * pvParameters, char * msg);
void self_test_reset(void);

#endif
