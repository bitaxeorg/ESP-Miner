/* Compile the production ASIC common code with controllable UART and timer
 * boundaries so receive_work() can be tested without hardware. */
#include <stdint.h>

int64_t serial_frame_test_get_time(void);
int16_t serial_frame_test_uart_rx(uint8_t *buffer, uint16_t size,
                                  uint16_t timeout_ms);

#define clear_asic_chain_error serial_frame_test_clear_asic_chain_error
#define get_asic_chain_error serial_frame_test_get_asic_chain_error
#define _reverse_bits serial_frame_test_reverse_bits
#define _largest_power_of_two serial_frame_test_largest_power_of_two
#define _next_power_of_two serial_frame_test_next_power_of_two
#define count_asic_chips_with_id_alias serial_frame_test_count_asic_chips_with_id_alias
#define count_asic_chips serial_frame_test_count_asic_chips
#define receive_work serial_frame_test_receive_work
#define get_difficulty_mask serial_frame_test_get_difficulty_mask
#define calculate_bm_timeout_ms serial_frame_test_calculate_bm_timeout_ms
#define SERIAL_rx serial_frame_test_uart_rx
#define esp_timer_get_time serial_frame_test_get_time

#include "../asic_common.c"
