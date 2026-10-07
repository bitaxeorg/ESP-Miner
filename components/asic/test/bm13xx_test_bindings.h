#ifndef BM13XX_TEST_BINDINGS_H
#define BM13XX_TEST_BINDINGS_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Test-build names keep these instances separate from the firmware drivers.
 * Only external I/O and timing are replaced; no driver logic is compiled out. */
#ifdef vTaskDelay
#undef vTaskDelay
#endif
#define vTaskDelay bm13xx_spy_delay
#define SERIAL_send bm13xx_spy_serial_send
#define SERIAL_set_baud bm13xx_stub_serial_set_baud
#define SERIAL_clear_buffer bm13xx_stub_serial_clear_buffer
#define receive_work bm13xx_fake_receive_work
#define count_asic_chips bm13xx_stub_count_chips
#define count_asic_chips_with_id_alias bm13xx_stub_count_chips_with_alias
#define do_frequency_transition bm13xx_stub_frequency_transition

void bm13xx_spy_delay(TickType_t ticks);

#define BM1366_init bm1366_test_init
#define BM1366_send_work bm1366_test_send_work
#define BM1366_set_version_mask bm1366_test_set_version_mask
#define BM1366_set_max_baud bm1366_test_set_max_baud
#define BM1366_send_hash_frequency bm1366_test_send_hash_frequency
#define BM1366_process_work bm1366_test_process_work
#define BM1366_read_registers bm1366_test_read_registers
#define BM1366_set_nonce_space bm1366_test_set_nonce_space
#define BM1366_set_hash_counting_number bm1366_test_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1366_set_hash_counting_number(uint32_t hcn);

#define BM1368_init bm1368_test_init
#define BM1368_send_work bm1368_test_send_work
#define BM1368_set_version_mask bm1368_test_set_version_mask
#define BM1368_set_max_baud bm1368_test_set_max_baud
#define BM1368_send_hash_frequency bm1368_test_send_hash_frequency
#define BM1368_process_work bm1368_test_process_work
#define BM1368_read_registers bm1368_test_read_registers
#define BM1368_set_nonce_space bm1368_test_set_nonce_space
#define BM1368_set_hash_counting_number bm1368_test_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1368_set_hash_counting_number(uint32_t hcn);

#define BM1370_init bm1370_test_init
#define BM1370_send_work bm1370_test_send_work
#define BM1370_set_version_mask bm1370_test_set_version_mask
#define BM1370_set_max_baud bm1370_test_set_max_baud
#define BM1370_send_hash_frequency bm1370_test_send_hash_frequency
#define BM1370_process_work bm1370_test_process_work
#define BM1370_read_registers bm1370_test_read_registers
#define BM1370_set_nonce_space bm1370_test_set_nonce_space
#define BM1370_set_hash_counting_number bm1370_test_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1370_set_hash_counting_number(uint32_t hcn);

#define BM1373_init bm1373_test_init
#define BM1373_send_work bm1373_test_send_work
#define BM1373_set_version_mask bm1373_test_set_version_mask
#define BM1373_set_max_baud bm1373_test_set_max_baud
#define BM1373_send_hash_frequency bm1373_test_send_hash_frequency
#define BM1373_process_work bm1373_test_process_work
#define BM1373_read_registers bm1373_test_read_registers
#define BM1373_set_nonce_space bm1373_test_set_nonce_space
#define BM1373_set_hash_counting_number bm1373_test_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1373_set_hash_counting_number(uint32_t hcn);

#endif
