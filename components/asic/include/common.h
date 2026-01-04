#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum
{
    REGISTER_INVALID = 0,
    REGISTER_HASHRATE,       // hashrate register (BM1397)
    REGISTER_TOTAL_COUNT,    // total counter (BM1366,BM1368,BM1370)
    REGISTER_DOMAIN_0_COUNT, // domain counters (BM1366,BM1368,BM1370)
    REGISTER_DOMAIN_1_COUNT,
    REGISTER_DOMAIN_2_COUNT,
    REGISTER_DOMAIN_3_COUNT,
    REGISTER_ERROR_COUNT,    // error count register (all)
    REGISTER_PLL_PARAM,      // PLL/clock config readback (BM1370)
} register_type_t;

typedef struct
{
    // -- job result response
    uint8_t job_id;
    uint32_t nonce;
    uint32_t rolled_version;
    // ---- register response
    register_type_t register_type;
    uint8_t asic_nr;
    uint32_t value;
} task_result;


unsigned char _reverse_bits(unsigned char num);
int _largest_power_of_two(int num);

int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length);
esp_err_t receive_work(uint8_t * buffer, int buffer_size);
void get_difficulty_mask(uint16_t difficulty, uint8_t *job_difficulty_mask);

#endif /* COMMON_H_ */
