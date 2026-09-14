#ifndef BM1397_TEST_BINDINGS_H
#define BM1397_TEST_BINDINGS_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef vTaskDelay
#undef vTaskDelay
#endif
#define vTaskDelay bm1397_stub_delay
#define SERIAL_send bm1397_spy_serial_send
#define SERIAL_set_baud bm1397_stub_serial_set_baud
#define SERIAL_clear_buffer bm1397_stub_serial_clear_buffer
#define receive_work bm1397_fake_receive_work
#define count_asic_chips bm1397_stub_count_chips
#define do_frequency_transition bm1397_stub_frequency_transition

void bm1397_stub_delay(TickType_t ticks);

#define BM1397_init bm1397_test_init
#define BM1397_send_work bm1397_test_send_work
#define BM1397_set_version_mask bm1397_test_set_version_mask
#define BM1397_set_max_baud bm1397_test_set_max_baud
#define BM1397_send_hash_frequency bm1397_test_send_hash_frequency
#define BM1397_process_work bm1397_test_process_work
#define BM1397_read_registers bm1397_test_read_registers

#endif
