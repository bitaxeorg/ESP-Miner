#include "bzm/job.h"

#include <string.h>

#include "bzm/hash.h"
#include "bm_job_midstate.h"

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
    work->handle = source->handle;
    work->engine_id = engine_id;
    work->timestamp_count = timestamp_count;
    work->starting_nonce = template->starting_nonce;
    work->end_nonce = UINT32_MAX;
    work->start_ntime = template->ntime;
    work->target = template->nbits;
    work->logical_sequence = logical_sequence;
    work->lead_zeros = lead_zeros;

    const uint8_t *tail = template->merkle_root + 28;
    work->merkle_residue = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) |
                          ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
    uint8_t midstate_data[80];
    uint8_t digest[32];

    /* Bonanza always uses four FIFO entries. Without version rolling they
     * share the base version; otherwise consume consecutive negotiated bits. */
    uint32_t version = template->version;
    for (size_t i = 0; i < BZM_VERSION_VARIANTS; ++i) {
        if (i != 0 && template->version_mask != 0)
            version = bzm_increment_version(version, template->version_mask);
        work->versions[i] = version;
        asic_job_header(template, template->starting_nonce, version, midstate_data);
        midstate_sha256_bin(midstate_data, 64, digest);
        /* SHA-256 state words are encoded as big-endian bytes.
         * BIRDS/cgminer writes native little-endian uint32_t h0..h7 words to
         * Bonanza, so swap bytes within each word while preserving the word
         * order. Bitmain-family packets instead reverse the word order. */
        bzm_reverse_word_bytes(digest);
        memcpy(work->midstates[i], digest, sizeof(digest));
    }
    return true;
}
