#include "asic_job.h"
#include <string.h>

static void write_le32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = (uint8_t)(value >> (8 * i));
    }
}

void asic_job_header(const asic_job_t *job, uint32_t nonce,
                     uint32_t version, uint8_t header[80])
{
    write_le32(header, version);
    memcpy(header + 4, job->prev_hash, 32);
    memcpy(header + 36, job->merkle_root, 32);
    write_le32(header + 68, job->ntime);
    write_le32(header + 72, job->nbits);
    write_le32(header + 76, nonce);
}
