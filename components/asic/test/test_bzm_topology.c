#include <stdlib.h>
#include <string.h>

#include "bzm/topology.h"
#include "unity.h"

static size_t coordinate_slot(uint8_t row, uint8_t column)
{
    return (size_t) column * BZM_TOPOLOGY_ROWS + row;
}

static void assert_same_engine(const bzm_engine_location_t * expected, const bzm_engine_location_t * actual)
{
    TEST_ASSERT_EQUAL_UINT8(expected->row, actual->row);
    TEST_ASSERT_EQUAL_UINT8(expected->column, actual->column);
    TEST_ASSERT_EQUAL_INT(expected->stack, actual->stack);
    TEST_ASSERT_EQUAL_UINT16(expected->physical_id, actual->physical_id);
    TEST_ASSERT_EQUAL_UINT16(expected->topology_index, actual->topology_index);
    TEST_ASSERT_EQUAL_UINT16(expected->stack_index, actual->stack_index);
}

TEST_CASE("BZM ASIC topology uses the spaced TDM wire IDs", "[asic][bzm][topology][tdm]")
{
    static const uint8_t expected[BZM_MAX_ASIC_COUNT] = {0x0a, 0x14, 0x1e, 0x28};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bzm_asic_wire_ids, BZM_MAX_ASIC_COUNT);
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        size_t round_trip = BZM_MAX_ASIC_COUNT;

        TEST_ASSERT_EQUAL_HEX8(expected[index], asic_id);
        TEST_ASSERT_TRUE(bzm_topology_asic_index(asic_id, &round_trip));
        TEST_ASSERT_EQUAL_UINT32(index, round_trip);
    }

    TEST_ASSERT_FALSE(bzm_topology_asic_index(0x00, NULL));
    TEST_ASSERT_FALSE(bzm_topology_asic_index(0x0b, NULL));
    TEST_ASSERT_FALSE(bzm_topology_asic_index(0x13, NULL));
    TEST_ASSERT_FALSE(bzm_topology_asic_index(0x29, NULL));
}

TEST_CASE("BZM 1002 topology contains exactly the 236 usable engines", "[asic][bzm][topology]")
{
    bool seen[BZM_TOPOLOGY_GRID_ENGINE_COUNT] = {false};
    size_t bottom_count = 0;
    size_t top_count = 0;

    for (uint16_t index = 0; index < BZM_TOPOLOGY_ENGINE_COUNT; ++index) {
        bzm_engine_location_t engine;
        /* Iterate physical coordinates in the compact-ID order; explicitly
         * skip the four disabled locations in the hardware layout. */
        uint16_t grid = index;
        if (grid >= 80) ++grid;
        if (grid >= 100) ++grid;
        if (grid >= 119) ++grid;
        uint8_t row = grid % BZM_TOPOLOGY_ROWS;
        uint8_t column = grid / BZM_TOPOLOGY_ROWS;
        TEST_ASSERT_TRUE(bzm_topology_from_physical_id(((uint16_t)column << 6) | row, &engine));
        TEST_ASSERT_EQUAL_UINT16(index, engine.topology_index);
        TEST_ASSERT_TRUE(bzm_topology_coordinate_is_valid(engine.row, engine.column));
        TEST_ASSERT_EQUAL_UINT16(((uint16_t) engine.column << 6) | engine.row, engine.physical_id);

        size_t slot = coordinate_slot(engine.row, engine.column);
        TEST_ASSERT_FALSE(seen[slot]);
        seen[slot] = true;

        if (engine.stack == BZM_ENGINE_STACK_BOTTOM) {
            TEST_ASSERT_LESS_THAN_UINT8(10, engine.row);
            ++bottom_count;
        } else {
            TEST_ASSERT_EQUAL_INT(BZM_ENGINE_STACK_TOP, engine.stack);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT8(10, engine.row);
            ++top_count;
        }
    }

    TEST_ASSERT_EQUAL_UINT16(BZM_TOPOLOGY_STACK_ENGINE_COUNT, bottom_count);
    TEST_ASSERT_EQUAL_UINT16(BZM_TOPOLOGY_STACK_ENGINE_COUNT, top_count);

    for (uint8_t column = 0; column < BZM_TOPOLOGY_COLUMNS; ++column) {
        for (uint8_t row = 0; row < BZM_TOPOLOGY_ROWS; ++row) {
            TEST_ASSERT_EQUAL(bzm_topology_coordinate_is_valid(row, column), seen[coordinate_slot(row, column)]);
        }
    }
}

TEST_CASE("BZM 1002 topology rejects all four disabled engines", "[asic][bzm][topology]")
{
    static const uint8_t holes[][2] = {
        {0, 4},
        {0, 5},
        {19, 5},
        {19, 11},
    };
    bzm_engine_location_t engine;

    for (size_t i = 0; i < sizeof(holes) / sizeof(holes[0]); ++i) {
        uint8_t row = holes[i][0];
        uint8_t column = holes[i][1];
        TEST_ASSERT_FALSE(bzm_topology_coordinate_is_valid(row, column));

        TEST_ASSERT_FALSE(bzm_topology_from_physical_id(((uint16_t) column << 6) | row, &engine));
    }
}

TEST_CASE("BZM topology identifiers round trip for every usable engine", "[asic][bzm][topology]")
{
    for (uint16_t index = 0; index < BZM_TOPOLOGY_ENGINE_COUNT; ++index) {
        bzm_engine_location_t expected;
        bzm_engine_location_t actual;
        TEST_ASSERT_TRUE(bzm_topology_activation_at(index, BZM_ENGINE_STACK_BOTTOM, &expected));

        TEST_ASSERT_TRUE(bzm_topology_from_physical_id(expected.physical_id, &actual));
        assert_same_engine(&expected, &actual);

        TEST_ASSERT_TRUE(bzm_topology_stack_engine_at(expected.stack, expected.stack_index, &actual));
        assert_same_engine(&expected, &actual);
    }
}

TEST_CASE("BZM balanced pairs cover both stacks exactly once", "[asic][bzm][topology][balance]")
{
    bool seen[BZM_TOPOLOGY_GRID_ENGINE_COUNT] = {false};

    for (uint16_t index = 0; index < BZM_TOPOLOGY_PAIR_COUNT; ++index) {
        bzm_engine_pair_t pair;
        TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(index, &pair));
        TEST_ASSERT_EQUAL_UINT16(index, pair.pair_index);
        TEST_ASSERT_EQUAL_INT(BZM_ENGINE_STACK_BOTTOM, pair.bottom.stack);
        TEST_ASSERT_EQUAL_INT(BZM_ENGINE_STACK_TOP, pair.top.stack);
        TEST_ASSERT_EQUAL_UINT16(index, pair.bottom.stack_index);
        TEST_ASSERT_EQUAL_UINT16(index, pair.top.stack_index);

        size_t bottom_slot = coordinate_slot(pair.bottom.row, pair.bottom.column);
        size_t top_slot = coordinate_slot(pair.top.row, pair.top.column);
        TEST_ASSERT_FALSE(seen[bottom_slot]);
        TEST_ASSERT_FALSE(seen[top_slot]);
        seen[bottom_slot] = true;
        seen[top_slot] = true;
    }

    for (uint8_t column = 0; column < BZM_TOPOLOGY_COLUMNS; ++column) {
        for (uint8_t row = 0; row < BZM_TOPOLOGY_ROWS; ++row) {
            TEST_ASSERT_EQUAL(bzm_topology_coordinate_is_valid(row, column), seen[coordinate_slot(row, column)]);
        }
    }
}

static void assert_balanced_activation_order(bzm_engine_stack_t first_stack)
{
    bool seen[BZM_TOPOLOGY_GRID_ENGINE_COUNT] = {false};
    int bottom_count = 0;
    int top_count = 0;

    for (uint16_t index = 0; index < BZM_TOPOLOGY_ACTIVATION_COUNT; ++index) {
        bzm_engine_location_t engine;
        TEST_ASSERT_TRUE(bzm_topology_activation_at(index, first_stack, &engine));

        bzm_engine_stack_t expected_stack =
            (index % 2U) == 0U ? first_stack
                               : (first_stack == BZM_ENGINE_STACK_BOTTOM ? BZM_ENGINE_STACK_TOP : BZM_ENGINE_STACK_BOTTOM);
        TEST_ASSERT_EQUAL_INT(expected_stack, engine.stack);
        TEST_ASSERT_EQUAL_UINT16(index / 2U, engine.stack_index);

        size_t slot = coordinate_slot(engine.row, engine.column);
        TEST_ASSERT_FALSE(seen[slot]);
        seen[slot] = true;

        if (engine.stack == BZM_ENGINE_STACK_BOTTOM) {
            ++bottom_count;
        } else {
            ++top_count;
        }
        TEST_ASSERT_LESS_OR_EQUAL_INT(1, abs(bottom_count - top_count));
    }

    TEST_ASSERT_EQUAL_INT(BZM_TOPOLOGY_STACK_ENGINE_COUNT, bottom_count);
    TEST_ASSERT_EQUAL_INT(BZM_TOPOLOGY_STACK_ENGINE_COUNT, top_count);
}

TEST_CASE("BZM activation order remains balanced from either stack", "[asic][bzm][topology][balance]")
{
    assert_balanced_activation_order(BZM_ENGINE_STACK_BOTTOM);
    assert_balanced_activation_order(BZM_ENGINE_STACK_TOP);
}

TEST_CASE("BZM topology APIs reject invalid input", "[asic][bzm][topology]")
{
    bzm_engine_location_t engine;
    bzm_engine_pair_t pair;

    TEST_ASSERT_FALSE(bzm_topology_coordinate_is_valid(20, 0));
    TEST_ASSERT_FALSE(bzm_topology_coordinate_is_valid(0, 12));

    TEST_ASSERT_FALSE(bzm_topology_from_physical_id(20, &engine));
    TEST_ASSERT_FALSE(bzm_topology_from_physical_id(12U << 6, &engine));
    TEST_ASSERT_FALSE(bzm_topology_from_physical_id(0, NULL));
    TEST_ASSERT_FALSE(bzm_topology_stack_engine_at(BZM_ENGINE_STACK_COUNT, 0, &engine));
    TEST_ASSERT_FALSE(bzm_topology_stack_engine_at(BZM_ENGINE_STACK_BOTTOM, BZM_TOPOLOGY_STACK_ENGINE_COUNT, &engine));
    TEST_ASSERT_FALSE(bzm_topology_stack_engine_at(BZM_ENGINE_STACK_BOTTOM, 0, NULL));
    TEST_ASSERT_FALSE(bzm_topology_balanced_pair_at(BZM_TOPOLOGY_PAIR_COUNT, &pair));
    TEST_ASSERT_FALSE(bzm_topology_balanced_pair_at(0, NULL));
    TEST_ASSERT_FALSE(bzm_topology_activation_at(BZM_TOPOLOGY_ACTIVATION_COUNT, BZM_ENGINE_STACK_BOTTOM, &engine));
    TEST_ASSERT_FALSE(bzm_topology_activation_at(0, BZM_ENGINE_STACK_COUNT, &engine));
    TEST_ASSERT_FALSE(bzm_topology_activation_at(0, BZM_ENGINE_STACK_BOTTOM, NULL));
}
