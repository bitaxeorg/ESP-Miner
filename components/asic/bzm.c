#include "bzm/protocol.h"

#include <string.h>

#include "mining.h"
#include "bm_job_midstate.h"
#include "utils.h"

bool bzm_work_build(const bzm_work_ref_t *source, uint16_t engine_id,
                    uint8_t logical_sequence, uint8_t timestamp_count,
                    uint8_t lead_zeros,
                    bzm_work_t *work)
{
    if (source == NULL || source->template == NULL || work == NULL ||
        source->handle == BZM_WORK_HANDLE_INVALID ||
        engine_id >= BZM_MAX_ENGINE_COUNT || timestamp_count > 0x7f ||
        source->template->ntime > UINT32_MAX - timestamp_count) {
        return false;
    }

    const asic_job_t *template = source->template;
    memset(work, 0, sizeof(*work));
    work->source = *source;
    work->engine_id = engine_id;
    work->timestamp_count = timestamp_count;
    work->starting_nonce = template->starting_nonce;
    work->end_nonce = UINT32_MAX;
    work->start_ntime = template->ntime;
    work->target = template->nbits;
    work->logical_sequence = logical_sequence;
    work->lead_zeros = lead_zeros;

    memcpy(&work->merkle_residue, template->merkle_root + 28,
           sizeof(work->merkle_residue));
    uint8_t midstate_data[80];
    uint8_t digest[32];

    /* Bonanza always uses four FIFO entries. Without version rolling they
     * share the base version; otherwise consume consecutive negotiated bits. */
    uint32_t version = template->version;
    for (size_t i = 0; i < BZM_VERSION_VARIANTS; ++i) {
        if (i != 0 && template->version_mask != 0)
            version = increment_bitmask(version, template->version_mask);
        work->versions[i] = version;
        asic_job_header(template, template->starting_nonce, version, midstate_data);
        midstate_sha256_bin(midstate_data, 64, digest);
        /* mbedTLS exposes each SHA-256 state word as big-endian bytes.
         * BIRDS/cgminer writes native little-endian uint32_t h0..h7 words to
         * Bonanza, so swap bytes within each word while preserving the word
         * order. Bitmain-family packets instead reverse the word order. */
        reverse_endianness_per_word(digest);
        memcpy(work->midstates[i], digest, sizeof(digest));
    }
    return true;
}

bool bzm_result_decode(const uint8_t frame[BZM_RESULT_FRAME_SIZE],
                       uint64_t timestamp_us, bzm_raw_result_t *result)
{
    if (frame == NULL || result == NULL) return false;

    uint16_t engine_status = ((uint16_t)frame[0] << 8) |
                             (uint16_t)frame[1];
    *result = (bzm_raw_result_t) {
        .engine_id = engine_status & 0x0fff,
        .status = engine_status >> 12,
        .nonce = (uint32_t)frame[2] |
                 ((uint32_t)frame[3] << 8) |
                 ((uint32_t)frame[4] << 16) |
                 ((uint32_t)frame[5] << 24),
        .sequence_id = frame[6],
        .time = frame[7],
        .timestamp_us = timestamp_us,
    };
    return result->engine_id < BZM_MAX_ENGINE_COUNT;
}

bool bzm_raw_result_has_valid_nonce(const bzm_raw_result_t *result)
{
    return result != NULL && (result->status & 0x08U) != 0U;
}

bool bzm_engine_logical_id(uint16_t physical_engine_id,
                           uint16_t *logical_engine_id)
{
    bzm_engine_location_t engine;
    if (logical_engine_id == NULL ||
        !bzm_topology_from_physical_id(physical_engine_id, &engine)) {
        return false;
    }
    *logical_engine_id = engine.topology_index;
    return true;
}
