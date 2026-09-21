#ifndef BZM_TOPOLOGY_H
#define BZM_TOPOLOGY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/chain.h"

#define BZM_ASIC_ID_STRIDE 10U
#define BZM_FIRST_ASIC_ID 0x0aU
#define BZM_LAST_ASIC_ID 0x28U

#define BZM_TOPOLOGY_ROWS 20U
#define BZM_TOPOLOGY_COLUMNS 12U
#define BZM_TOPOLOGY_GRID_ENGINE_COUNT (BZM_TOPOLOGY_ROWS * BZM_TOPOLOGY_COLUMNS)
#define BZM_TOPOLOGY_ENGINE_COUNT 236U
#define BZM_TOPOLOGY_STACK_ENGINE_COUNT 118U
#define BZM_TOPOLOGY_PAIR_COUNT BZM_TOPOLOGY_STACK_ENGINE_COUNT
#define BZM_TOPOLOGY_ACTIVATION_COUNT BZM_TOPOLOGY_ENGINE_COUNT

typedef enum
{
    BZM_ENGINE_STACK_BOTTOM = 0,
    BZM_ENGINE_STACK_TOP = 1,
    BZM_ENGINE_STACK_COUNT = 2,
} bzm_engine_stack_t;

/**
 * A usable Bonanza engine and all of its stable identifiers.
 *
 * topology_index and stack_index are compact, excluding disabled engines.
 */
typedef struct
{
    uint8_t row;
    uint8_t column;
    bzm_engine_stack_t stack;
    uint16_t physical_id;
    uint16_t topology_index;
    uint16_t stack_index;
} bzm_engine_location_t;

/**
 * A scheduling pair containing one usable engine from each voltage stack.
 * Pairing is deterministic but does not imply physical mirroring.
 */
typedef struct
{
    uint16_t pair_index;
    bzm_engine_location_t bottom;
    bzm_engine_location_t top;
} bzm_engine_pair_t;

/* Wire IDs are deliberately separated by ten TDM slots. Keep logical ASIC
 * indexes dense for software bookkeeping; never infer one from adjacent wire
 * IDs. This mapping matches the BIRDS reference firmware. */
extern const uint8_t bzm_asic_wire_ids[BZM_MAX_ASIC_COUNT];

bool bzm_topology_asic_index(uint8_t asic_id, size_t * logical_index);

bool bzm_topology_coordinate_is_valid(uint8_t row, uint8_t column);

bool bzm_topology_from_physical_id(uint16_t physical_id, bzm_engine_location_t * engine);

bool bzm_topology_stack_engine_at(bzm_engine_stack_t stack, uint16_t stack_index, bzm_engine_location_t * engine);

bool bzm_topology_balanced_pair_at(uint16_t pair_index, bzm_engine_pair_t * pair);

/**
 * Return an engine from a deterministic balanced activation sequence.
 *
 * The selected first_stack is emitted first for each pair. Every prefix of
 * the returned order differs by at most one active engine between stacks.
 */
bool bzm_topology_activation_at(uint16_t activation_index, bzm_engine_stack_t first_stack, bzm_engine_location_t * engine);

#endif // BZM_TOPOLOGY_H
