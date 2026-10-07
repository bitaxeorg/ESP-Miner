#ifndef RESULT_TASK_TEST_BINDINGS_H_
#define RESULT_TASK_TEST_BINDINGS_H_

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef vTaskDelay
#undef vTaskDelay
#endif
#define vTaskDelay result_task_spy_delay
#define ASIC_result_task result_task_test_run
#define ASIC_process_work result_task_fake_process_work
#define stratum_submit_share result_task_fake_submit_share
#define self_test_record_nonce result_task_spy_record_nonce
#define SYSTEM_notify_found_nonce result_task_spy_notify_found_nonce
#define scoreboard_add result_task_spy_scoreboard_add
#define hashrate_monitor_register_read result_task_spy_register_read

void result_task_spy_delay(TickType_t ticks);

#include "../../../main/tasks/asic_result_task.h"

#endif /* RESULT_TASK_TEST_BINDINGS_H_ */
