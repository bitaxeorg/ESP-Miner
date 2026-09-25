#ifndef BZM_H
#define BZM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/result.h"
#include "bzm/work_ref.h"
#include "bzm/topology.h"

#define BZM_CHIP_ID 0xB0A0U
#define BZM_BAUD_RATE 2000000
#define BZM_RESULT_FRAME_SIZE 8
#define BZM_TDM_RESULT_FRAME_SIZE (BZM_RESULT_FRAME_SIZE + 2)
#define BZM_MAX_ENGINE_COUNT 4096
#define BZM_VERSION_VARIANTS 4
/* Qualified on the Bitaxe 1002 with the 0x04 engine profile. Seven
 * independent hardware results reproduced their ASIC 34-bit filter exactly
 * after subtracting 0x4c; the former 0x28 assumption reproduced none. */
#define BZM_NONCE_GAP_1002 0x4cU
#define BZM_ENGINE_ROWS BZM_TOPOLOGY_ROWS
#define BZM_ENGINE_COLUMNS BZM_TOPOLOGY_COLUMNS
#define BZM_ENGINE_GRID_COUNT BZM_TOPOLOGY_GRID_ENGINE_COUNT
#define BZM_ENGINES_PER_ASIC BZM_TOPOLOGY_ENGINE_COUNT
#define BZM_MAX_ACTIVE_WORK 256U
#define BZM_CONTROL_ENGINE_ID 0xfff
/* 0xfa addresses the next unassigned ASIC during chain discovery. The BIRDS
 * protocol uses 0xff for a write that every already-addressed ASIC consumes. */
#define BZM_BROADCAST_ASIC 0xfaU
#define BZM_ALL_ASICS 0xffU

typedef struct {
    bzm_work_ref_t source;
    uint16_t engine_id;
    uint8_t timestamp_count;
    uint32_t starting_nonce;
    uint32_t end_nonce;
    uint8_t midstates[BZM_VERSION_VARIANTS][32];
    uint32_t versions[BZM_VERSION_VARIANTS];
    uint32_t merkle_residue;
    uint32_t start_ntime;
    uint32_t target;
    uint8_t logical_sequence;
    uint8_t lead_zeros;
} bzm_work_t;

typedef struct {
    uint8_t asic_id;
    uint16_t engine_id;
    uint8_t status;
    uint32_t nonce;
    uint8_t sequence_id;
    uint8_t time;
    uint64_t timestamp_us;
} bzm_raw_result_t;

bool bzm_work_build(const bzm_work_ref_t *source, uint16_t engine_id,
                    uint8_t logical_sequence, uint8_t timestamp_count,
                    uint8_t lead_zeros,
                    bzm_work_t *work);

bool bzm_result_decode(const uint8_t frame[BZM_RESULT_FRAME_SIZE],
                       uint64_t timestamp_us, bzm_raw_result_t *result);
bool bzm_raw_result_has_valid_nonce(const bzm_raw_result_t *result);
bool bzm_engine_logical_id(uint16_t physical_engine_id,
                           uint16_t *logical_engine_id);

#endif // BZM_H
