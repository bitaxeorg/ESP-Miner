#include "bitmain_job_packet.h"
#include "bm_job_midstate.h"
#include "mining.h"
#include "utils.h"
#include <string.h>

_Static_assert(sizeof(bm13xx_job_packet_t) == 82, "BM13xx wire payload size");
_Static_assert(sizeof(bm1397_job_packet_t) == 146, "BM1397 wire payload size");

static void write_le32(uint8_t dest[4], uint32_t value)
{
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

void bm13xx_build_job_packet(const asic_job_t *job, uint8_t job_id,
                             bm13xx_job_packet_t *packet)
{
    packet->job_id = job_id;
    packet->num_midstates = 1;
    write_le32(packet->starting_nonce, job->starting_nonce);
    write_le32(packet->nbits, job->nbits);
    write_le32(packet->ntime, job->ntime);
    reverse_32bit_words(job->merkle_root, packet->merkle_root);
    reverse_32bit_words(job->prev_hash, packet->prev_block_hash);
    write_le32(packet->version, job->version);
}

void bm1397_build_job_packet(const asic_job_t *job, uint8_t job_id,
                             uint8_t software_midstates,
                             bm1397_job_packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
    packet->job_id = job_id;
    write_le32(packet->starting_nonce, job->starting_nonce);
    write_le32(packet->nbits, job->nbits);
    write_le32(packet->ntime, job->ntime);
    memcpy(packet->merkle4, job->merkle_root + 28, sizeof(packet->merkle4));

    uint8_t header[80];
    uint8_t midstate[32];
    uint32_t version = job->version;
    for (uint8_t i = 0; i < software_midstates && i < BM1397_NUM_MIDSTATES; ++i) {
        if (i > 0) {
            if (job->version_mask == 0) {
                break;
            }
            version = increment_bitmask(version, job->version_mask);
        }
        asic_job_header(job, job->starting_nonce, version, header);
        midstate_sha256_bin(header, 64, midstate);
        reverse_32bit_words(midstate, packet->midstates[i]);
        ++packet->num_midstates;
    }
}
