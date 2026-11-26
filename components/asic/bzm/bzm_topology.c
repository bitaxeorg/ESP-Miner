#include "bzm/topology.h"

#include <stddef.h>

_Static_assert(BZM_TOPOLOGY_ENGINE_COUNT == BZM_TOPOLOGY_STACK_ENGINE_COUNT * BZM_ENGINE_STACK_COUNT,
               "Bonanza stacks must contain equal usable engine counts");

const uint8_t bzm_asic_wire_ids[BZM_MAX_ASIC_COUNT] = {0x0aU, 0x14U, 0x1eU, 0x28U};

_Static_assert(BZM_FIRST_ASIC_ID == BZM_ASIC_ID_STRIDE, "first BZM wire ID must occupy TDM slot ten");
_Static_assert(BZM_LAST_ASIC_ID == BZM_ASIC_ID_STRIDE * BZM_MAX_ASIC_COUNT,
               "last BZM wire ID must match the spaced four-ASIC topology");

bool bzm_topology_asic_index(uint8_t asic_id, size_t * logical_index)
{
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        if (bzm_asic_wire_ids[index] == asic_id) {
            if (logical_index != NULL) {
                *logical_index = index;
            }
            return true;
        }
    }
    return false;
}

static bool stack_is_valid(bzm_engine_stack_t stack)
{
    return stack == BZM_ENGINE_STACK_BOTTOM || stack == BZM_ENGINE_STACK_TOP;
}

enum { ROWS_PER_STACK = BZM_TOPOLOGY_ROWS / BZM_ENGINE_STACK_COUNT };

static bzm_engine_stack_t stack_for_row(uint8_t row)
{
    return row < ROWS_PER_STACK ? BZM_ENGINE_STACK_BOTTOM : BZM_ENGINE_STACK_TOP;
}

/* Disabled engines in column-major order. Compact indexes skip these slots. */
static const struct {
    uint8_t row;
    uint8_t column;
} disabled_engines[] = {
    {0, 4}, {0, 5}, {19, 5}, {19, 11},
};

enum { DISABLED_ENGINE_COUNT = sizeof(disabled_engines) / sizeof(disabled_engines[0]) };
_Static_assert(BZM_TOPOLOGY_ENGINE_COUNT + DISABLED_ENGINE_COUNT == BZM_TOPOLOGY_GRID_ENGINE_COUNT,
               "disabled engine list must account for every missing coordinate");

static uint16_t grid_index(uint8_t row, uint8_t column)
{
    return column * BZM_TOPOLOGY_ROWS + row;
}

static uint16_t stack_grid_index(uint8_t row, uint8_t column)
{
    return column * ROWS_PER_STACK + row % ROWS_PER_STACK;
}

bool bzm_topology_coordinate_is_valid(uint8_t row, uint8_t column)
{
    if (row >= BZM_TOPOLOGY_ROWS || column >= BZM_TOPOLOGY_COLUMNS) return false;
    for (size_t i = 0; i < DISABLED_ENGINE_COUNT; ++i) {
        if (row == disabled_engines[i].row && column == disabled_engines[i].column) return false;
    }
    return true;
}

static bool locate_coordinate(uint8_t row, uint8_t column, bzm_engine_location_t *engine)
{
    if (engine == NULL || row >= BZM_TOPOLOGY_ROWS || column >= BZM_TOPOLOGY_COLUMNS) {
        return false;
    }

    bzm_engine_stack_t stack = stack_for_row(row);
    uint16_t position = grid_index(row, column);
    uint16_t topology_index = position;
    uint16_t stack_index = stack_grid_index(row, column);
    for (size_t i = 0; i < DISABLED_ENGINE_COUNT; ++i) {
        uint16_t hole = grid_index(disabled_engines[i].row, disabled_engines[i].column);
        if (hole > position) break;
        if (hole == position) return false;
        --topology_index;
        if (stack_for_row(disabled_engines[i].row) == stack) --stack_index;
    }

    *engine = (bzm_engine_location_t){
        .row = row, .column = column, .stack = stack,
        .physical_id = ((uint16_t)column << 6) | row,
        .topology_index = topology_index, .stack_index = stack_index,
    };
    return true;
}

bool bzm_topology_from_physical_id(uint16_t physical_id, bzm_engine_location_t * engine)
{
    uint8_t row = physical_id & 0x3fU;
    uint16_t column = physical_id >> 6;
    if (column >= BZM_TOPOLOGY_COLUMNS)
        return false;

    return locate_coordinate(row, (uint8_t) column, engine);
}

bool bzm_topology_stack_engine_at(bzm_engine_stack_t stack, uint16_t stack_index, bzm_engine_location_t * engine)
{
    if (engine == NULL || !stack_is_valid(stack) || stack_index >= BZM_TOPOLOGY_STACK_ENGINE_COUNT) {
        return false;
    }

    uint16_t position = stack_index;
    for (size_t i = 0; i < DISABLED_ENGINE_COUNT; ++i) {
        uint8_t row = disabled_engines[i].row;
        if (stack_for_row(row) == stack && stack_grid_index(row, disabled_engines[i].column) <= position) {
            ++position;
        }
    }
    uint8_t row = position % ROWS_PER_STACK;
    if (stack == BZM_ENGINE_STACK_TOP) row += ROWS_PER_STACK;
    return locate_coordinate(row, position / ROWS_PER_STACK, engine);
}

bool bzm_topology_balanced_pair_at(uint16_t pair_index, bzm_engine_pair_t * pair)
{
    if (pair == NULL || pair_index >= BZM_TOPOLOGY_PAIR_COUNT) {
        return false;
    }

    bzm_engine_pair_t found = {.pair_index = pair_index};
    if (!bzm_topology_stack_engine_at(BZM_ENGINE_STACK_BOTTOM, pair_index, &found.bottom) ||
        !bzm_topology_stack_engine_at(BZM_ENGINE_STACK_TOP, pair_index, &found.top)) {
        return false;
    }

    *pair = found;
    return true;
}

bool bzm_topology_activation_at(uint16_t activation_index, bzm_engine_stack_t first_stack, bzm_engine_location_t * engine)
{
    if (engine == NULL || !stack_is_valid(first_stack) || activation_index >= BZM_TOPOLOGY_ACTIVATION_COUNT) {
        return false;
    }

    uint16_t pair_index = activation_index / BZM_ENGINE_STACK_COUNT;
    bool second_member = (activation_index % BZM_ENGINE_STACK_COUNT) != 0U;
    bzm_engine_stack_t stack = first_stack;
    if (second_member) {
        stack = first_stack == BZM_ENGINE_STACK_BOTTOM ? BZM_ENGINE_STACK_TOP : BZM_ENGINE_STACK_BOTTOM;
    }

    return bzm_topology_stack_engine_at(stack, pair_index, engine);
}
