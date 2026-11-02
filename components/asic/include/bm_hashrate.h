#ifndef BM_HASHRATE_H_
#define BM_HASHRATE_H_

#include <stdint.h>

/* Convert a Bitmain hash counter delta over duration_us to GH/s. */
float bm_hash_counter_to_ghs(uint64_t duration_us, uint32_t counter);

#endif /* BM_HASHRATE_H_ */
