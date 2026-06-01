#ifndef SELF_TEST_H_
#define SELF_TEST_H_

#include "esp_err.h"

esp_err_t self_test_init(void * pvParameters);
void self_test_task(void * pvParameters);
void self_test_show_message(void * pvParameters, const char * msg);
void self_test_reset(void);
void self_test_record_nonce(void * pvParameters, double nonce_diff);

#endif
