#ifndef LOG_BUFFER_H_
#define LOG_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

#define LOG_BUFFER_SIZE  (512 * 1024)  /* 512 KB */
#define RTC_LOG_BUFFER_SIZE (4 * 1024) /* 4 KB */
#define RTC_LOG_BASE_ADDR   0x600FE000 /* Start of RTC Fast RAM on ESP32-S3 */

typedef struct {
    uint32_t magic;
    uint32_t checksum;
    uint32_t len;
} rtc_log_header_t;

void log_buffer_early_init(void);
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
