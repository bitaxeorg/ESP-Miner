#ifndef BM_JOB_MIDSTATE_H_
#define BM_JOB_MIDSTATE_H_

#include <stdint.h>
#include <stddef.h>

/* One unpadded SHA-256 block for Bitmain software midstates. */
void midstate_sha256_bin(const uint8_t *data, size_t data_len, uint8_t dest[32]);

#endif
