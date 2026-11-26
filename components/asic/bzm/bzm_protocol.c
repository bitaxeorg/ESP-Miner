#include "bzm/protocol.h"

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
