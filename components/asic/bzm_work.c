#include "bzm_transport.h"
#include "bzm_work_registers.h"

#include <string.h>

static bool program_work(const bzm_work_t *work,
                         bzm_register_writer_t writer,
                         void *writer_context,
                         bool include_nonce_range)
{
    if (work == NULL || writer == NULL) {
        return false;
    }

    /* Match the production BIRDS encoding exactly: ZEROS_TO_FIND stores the
     * requested leading-zero count after the fixed 32-bit difficulty-one
     * prefix. This must stay aligned with BZM_record_local_result(), which
     * credits each result as 2^(lead_zeros - 32) difficulty-one units. */
    uint8_t zero_count = work->lead_zeros > 32
        ? (uint8_t)(work->lead_zeros - 32)
        : 0;
    if (zero_count > 32) zero_count = 32;
    uint8_t timestamp_control = work->timestamp_count | BZM_TIMESTAMP_AUTO_CLOCK_UNGATE;
    /* BIRDS/cgminer hands the ASIC the final three header words in the
     * per-word-swapped work->data representation. Keep bzm_work_t in host
     * semantics for result mapping and convert only at the register boundary. */
    uint32_t wire_merkle_residue = __builtin_bswap32(work->merkle_residue);
    uint32_t wire_start_ntime = __builtin_bswap32(work->start_ntime);
    uint32_t wire_target = __builtin_bswap32(work->target);
    if (!writer(writer_context, work->engine_id, BZM_REG_ZEROS_TO_FIND, &zero_count, 1) ||
        !writer(writer_context, work->engine_id, BZM_REG_TIMESTAMP_COUNT, &timestamp_control, 1) ||
        (include_nonce_range &&
         (!writer(writer_context, work->engine_id, BZM_REG_START_NONCE, &work->starting_nonce, 4) ||
          !writer(writer_context, work->engine_id, BZM_REG_END_NONCE, &work->end_nonce, 4))) ||
        !writer(writer_context, work->engine_id, BZM_REG_MERKLE_RESIDUE, &wire_merkle_residue, 4) ||
        !writer(writer_context, work->engine_id, BZM_REG_START_TIMESTAMP, &wire_start_ntime, 4) ||
        !writer(writer_context, work->engine_id, BZM_REG_TARGET, &wire_target, 4)) {
        return false;
    }

    // The BZM engine has separate midstate and sequence FIFOs. Match the
    // reference transport by filling each FIFO in order before starting work.
    for (size_t i = 0; i < BZM_VERSION_VARIANTS; ++i) {
        if (!writer(writer_context, work->engine_id, BZM_REG_MIDSTATE, work->midstates[i], 32)) {
            return false;
        }
    }
    for (size_t i = 0; i < BZM_VERSION_VARIANTS; ++i) {
        uint8_t sequence = (uint8_t) ((work->logical_sequence << 2) | i);
        if (!writer(writer_context, work->engine_id, BZM_REG_SEQUENCE_ID, &sequence, 1)) {
            return false;
        }
    }

    uint8_t job_control = 3;
    return writer(writer_context, work->engine_id, BZM_REG_JOB_CONTROL, &job_control, 1);
}

bool bzm_transport_program_work(const bzm_work_t *work,
                                bzm_register_writer_t writer,
                                void *writer_context)
{
    return program_work(work, writer, writer_context, true);
}

bool bzm_transport_program_broadcast_work(const bzm_work_t *work,
                                          bzm_register_writer_t writer,
                                          void *writer_context)
{
    return program_work(work, writer, writer_context, false);
}

static bool program_flush_job(uint16_t engine, uint8_t job_control, bzm_register_writer_t writer,
                              void * writer_context)
{
    uint8_t timestamp_count = 0xff;
    if (!writer(writer_context, engine, BZM_REG_TIMESTAMP_COUNT, &timestamp_count, 1)) {
        return false;
    }

    for (size_t i = 0; i < BZM_VERSION_VARIANTS; ++i) {
        uint8_t value = 0xfc + i;
        if (!writer(writer_context, engine, BZM_REG_SEQUENCE_ID, &value, 1)) {
            return false;
        }
    }
    return writer(writer_context, engine, BZM_REG_JOB_CONTROL, &job_control, 1);
}

bool bzm_transport_program_startup_work(uint16_t engine_id, bzm_register_writer_t writer, void * writer_context)
{
    if (engine_id >= BZM_MAX_ENGINE_COUNT || writer == NULL) {
        return false;
    }

    /* SHA-256 initialization words, varied per TCE. These are deterministic
     * startup load for stack balancing. A 64-leading-zero filter suppresses
     * results until ordinary pool work replaces it. */
    static const uint32_t sha256_initial_state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    bzm_work_t work = {
        .engine_id = engine_id,
        .timestamp_count = 0xff,
        .starting_nonce = 0,
        .end_nonce = UINT32_MAX,
        .merkle_residue = 0x1002b0a0U,
        .start_ntime = 0x65010002U,
        .target = 0x1d00ffffU,
        .logical_sequence = 0x3f,
        .lead_zeros = 64,
    };
    for (size_t variant = 0; variant < BZM_VERSION_VARIANTS; ++variant) {
        for (size_t word = 0; word < 8; ++word) {
            uint32_t value = sha256_initial_state[word] ^ ((uint32_t) variant * 0x01010101U);
            memcpy(&work.midstates[variant][word * sizeof(value)], &value, sizeof(value));
        }
    }

    return bzm_transport_program_work(&work, writer, writer_context) &&
           program_flush_job(engine_id, 1, writer, writer_context);
}

bool bzm_transport_program_flush(uint16_t engine_count, bzm_register_writer_t writer, void * writer_context)
{
    if (engine_count == 0 || engine_count > BZM_ENGINES_PER_ASIC || writer == NULL) {
        return false;
    }

    for (uint16_t logical_engine = 0; logical_engine < engine_count; ++logical_engine) {
        bzm_engine_location_t engine;
        // Balanced write order limits transient stack skew.
        if (!bzm_topology_activation_at(logical_engine, BZM_ENGINE_STACK_BOTTOM, &engine)) {
            return false;
        }
        if (!program_flush_job(engine.physical_id, 3, writer, writer_context) ||
            !program_flush_job(engine.physical_id, 1, writer, writer_context)) {
            return false;
        }
    }
    return true;
}
