#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include "esp_err.h"

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint32_t nonce;
    uint32_t rolled_version;
} task_result;

unsigned char _reverse_bits(unsigned char num);
int _largest_power_of_two(int num);

int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length);
esp_err_t receive_work(uint8_t * buffer, int buffer_size);

#endif /* COMMON_H_ */
