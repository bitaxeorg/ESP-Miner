#include "bm_job_packet.h"
#include "bm_job_midstate.h"
#include "mining.h"
#include "mining_job_bytes.h"
#include "utils.h"
#include <string.h>

static const char *TAG __attribute__((unused)) = "bm_job_packet";
#define BM_DEBUG_JOBS false

_Static_assert(sizeof(bm13xx_job_packet_t) == 82, "BM13xx wire payload size");
_Static_assert(sizeof(bm1397_job_packet_t) == 146, "BM1397 wire payload size");

void bm13xx_encode_job_packet(const asic_job_t *job, uint8_t chip_slot,
                              bm13xx_job_packet_t *packet)
{
    packet->chip_slot = chip_slot;
    packet->num_midstates = 1;
    write_le32(packet->starting_nonce, job->starting_nonce);
    write_le32(packet->nbits, job->nbits);
    write_le32(packet->ntime, job->ntime);
    reverse_32bit_words(job->merkle_root, packet->merkle_root);
    reverse_32bit_words(job->prev_hash, packet->prev_block_hash);
    write_le32(packet->version, job->version);
}

void bm1397_encode_job_packet(const asic_job_t *job, uint8_t chip_slot,
                              uint8_t software_midstates,
                              bm1397_job_packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
    packet->chip_slot = chip_slot;
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

void bm13xx_build_job_packet(asic_job_store_t *store,
                             const asic_job_t *job, bm13xx_job_packet_t *packet)
{
    uint8_t slot = (uint8_t)asic_job_store_claim(store, job);
    uint8_t chip_slot = bm_encode_wire_job_slot(slot, BM13XX_TX_SLOT_SHIFT);
    bm13xx_encode_job_packet(job, chip_slot, packet);
#if BM_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: slot %u (0x%02X)", slot, chip_slot);
#endif
}

void bm1397_build_job_packet(asic_job_store_t *store,
                             uint8_t software_midstates,
                             const asic_job_t *job, bm1397_job_packet_t *packet)
{
    uint8_t slot = (uint8_t)asic_job_store_claim(store, job);
    uint8_t chip_slot = bm_encode_wire_job_slot(slot, BM1397_TX_SLOT_SHIFT);
    bm1397_encode_job_packet(job, chip_slot, software_midstates, packet);
#if BM_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: slot %u (0x%02X)", slot, chip_slot);
#endif
}
