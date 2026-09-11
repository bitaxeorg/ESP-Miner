#ifndef BM13XX_FIXTURE_BINDINGS_H
#define BM13XX_FIXTURE_BINDINGS_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Test-build names keep these copies separate from firmware drivers in QEMU.
 * Only external I/O and timing are replaced; no driver logic is compiled out.
 * The host compiles each production source directly for accurate coverage. */
#ifdef vTaskDelay
#undef vTaskDelay
#endif
#define vTaskDelay bm13xx_fixture_delay
#define SERIAL_send bm13xx_fixture_serial_send
#define SERIAL_set_baud bm13xx_fixture_serial_set_baud
#define SERIAL_clear_buffer bm13xx_fixture_serial_clear_buffer
#define receive_work bm13xx_fixture_receive_work
#define count_asic_chips bm13xx_fixture_count_chips
#define count_asic_chips_with_id_alias bm13xx_fixture_count_chips_with_alias
#define do_frequency_transition bm13xx_fixture_frequency_transition

void bm13xx_fixture_delay(TickType_t ticks);

#define BM1366_init fixture_BM1366_init
#define BM1366_send_work fixture_BM1366_send_work
#define BM1366_set_version_mask fixture_BM1366_set_version_mask
#define BM1366_set_max_baud fixture_BM1366_set_max_baud
#define BM1366_send_hash_frequency fixture_BM1366_send_hash_frequency
#define BM1366_process_work fixture_BM1366_process_work
#define BM1366_read_registers fixture_BM1366_read_registers
#define BM1366_set_nonce_space fixture_BM1366_set_nonce_space
#define BM1366_set_hash_counting_number fixture_BM1366_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1366_set_hash_counting_number(uint32_t hcn);

#define BM1368_init fixture_BM1368_init
#define BM1368_send_work fixture_BM1368_send_work
#define BM1368_set_version_mask fixture_BM1368_set_version_mask
#define BM1368_set_max_baud fixture_BM1368_set_max_baud
#define BM1368_send_hash_frequency fixture_BM1368_send_hash_frequency
#define BM1368_process_work fixture_BM1368_process_work
#define BM1368_read_registers fixture_BM1368_read_registers
#define BM1368_set_nonce_space fixture_BM1368_set_nonce_space
#define BM1368_set_hash_counting_number fixture_BM1368_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1368_set_hash_counting_number(uint32_t hcn);

#define BM1370_init fixture_BM1370_init
#define BM1370_send_work fixture_BM1370_send_work
#define BM1370_set_version_mask fixture_BM1370_set_version_mask
#define BM1370_set_max_baud fixture_BM1370_set_max_baud
#define BM1370_send_hash_frequency fixture_BM1370_send_hash_frequency
#define BM1370_process_work fixture_BM1370_process_work
#define BM1370_read_registers fixture_BM1370_read_registers
#define BM1370_set_nonce_space fixture_BM1370_set_nonce_space
#define BM1370_set_hash_counting_number fixture_BM1370_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1370_set_hash_counting_number(uint32_t hcn);

#define BM1373_init fixture_BM1373_init
#define BM1373_send_work fixture_BM1373_send_work
#define BM1373_set_version_mask fixture_BM1373_set_version_mask
#define BM1373_set_max_baud fixture_BM1373_set_max_baud
#define BM1373_send_hash_frequency fixture_BM1373_send_hash_frequency
#define BM1373_process_work fixture_BM1373_process_work
#define BM1373_read_registers fixture_BM1373_read_registers
#define BM1373_set_nonce_space fixture_BM1373_set_nonce_space
#define BM1373_set_hash_counting_number fixture_BM1373_set_hash_counting_number
/* This existing driver function has no declaration in its public header. */
void BM1373_set_hash_counting_number(uint32_t hcn);

#endif
