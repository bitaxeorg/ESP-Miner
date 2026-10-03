#include "bm_hashrate.h"

#define BM_HASHES_PER_COUNTER_UNIT 0x100000000ULL

float bm_hash_counter_to_ghs(uint64_t duration_us, uint32_t counter)
{
    if (duration_us == 0) {
        return 0.0f;
    }
    float seconds = duration_us / 1000000.0;
    float hashes_per_second = counter / seconds * (float)BM_HASHES_PER_COUNTER_UNIT;
    return hashes_per_second / 1e9f;
}
