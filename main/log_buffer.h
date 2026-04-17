#ifndef LOG_BUFFER_H_
#define LOG_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

void log_buffer_init(void);
uint64_t log_buffer_get_total_written(void);

/**
 * @param abs_pos Pointer to a 64-bit absolute track position. 
 * @param dest Destination memory.
 * @param max_len Max chunk size to extract.
 * @return Bytes read into dest.
 */
size_t log_buffer_read_absolute(uint64_t *abs_pos, char *dest, size_t max_len);

#endif /* LOG_BUFFER_H_ */
