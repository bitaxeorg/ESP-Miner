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

static bzm_engine_stack_t stack_for_row(uint8_t row)
{
    return row < (BZM_TOPOLOGY_ROWS / 2U) ? BZM_ENGINE_STACK_BOTTOM : BZM_ENGINE_STACK_TOP;
}

bool bzm_topology_coordinate_is_valid(uint8_t row, uint8_t column)
{
    if (row >= BZM_TOPOLOGY_ROWS || column >= BZM_TOPOLOGY_COLUMNS) {
        return false;
    }

    return !((row == 0U && (column == 4U || column == 5U)) || (row == 19U && (column == 5U || column == 11U)));
}

static bool locate_coordinate(uint8_t requested_row, uint8_t requested_column, bzm_engine_location_t * engine)
{
    if (engine == NULL || !bzm_topology_coordinate_is_valid(requested_row, requested_column)) {
        return false;
    }

    uint16_t topology_index = 0;
    uint16_t bottom_index = 0;
    uint16_t top_index = 0;

    for (uint8_t column = 0; column < BZM_TOPOLOGY_COLUMNS; ++column) {
        for (uint8_t row = 0; row < BZM_TOPOLOGY_ROWS; ++row) {
            if (!bzm_topology_coordinate_is_valid(row, column))
                continue;

            bzm_engine_stack_t stack = stack_for_row(row);
            uint16_t stack_index = stack == BZM_ENGINE_STACK_BOTTOM ? bottom_index : top_index;

            if (row == requested_row && column == requested_column) {
                *engine = (bzm_engine_location_t){
                    .row = row,
                    .column = column,
                    .stack = stack,
                    .physical_id = ((uint16_t) column << 6) | row,
                    .topology_index = topology_index,
                    .stack_index = stack_index,
                };
                return true;
            }

            ++topology_index;
            if (stack == BZM_ENGINE_STACK_BOTTOM) {
                ++bottom_index;
            } else {
                ++top_index;
            }
        }
    }

    return false;
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

    uint16_t current = 0;
    for (uint8_t column = 0; column < BZM_TOPOLOGY_COLUMNS; ++column) {
        for (uint8_t row = 0; row < BZM_TOPOLOGY_ROWS; ++row) {
            if (!bzm_topology_coordinate_is_valid(row, column) || stack_for_row(row) != stack) {
                continue;
            }
            if (current == stack_index) {
                return locate_coordinate(row, column, engine);
            }
            ++current;
        }
    }

    return false;
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
