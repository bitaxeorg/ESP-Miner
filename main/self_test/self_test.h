#ifndef SELF_TEST_H_
#define SELF_TEST_H_

#include "esp_err.h"

typedef struct GlobalState GlobalState;

esp_err_t self_test_init(GlobalState * GLOBAL_STATE);
void self_test_task(void * pvParameters);
void self_test_show_message(GlobalState * GLOBAL_STATE, const char * msg);
void self_test_reset(void);
void self_test_record_nonce(GlobalState * GLOBAL_STATE, double nonce_diff);

#endif
