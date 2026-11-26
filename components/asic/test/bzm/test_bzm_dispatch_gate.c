#include "unity.h"

#include "bzm/dispatch_gate.h"

static bool authorize_from_flag(void * context)
{
    return context != NULL && *(bool *)context;
}

TEST_CASE("bzm dispatch authorization defaults closed", "[bzm_dispatch_gate]")
{
    bzm_dispatch_gate_t gate;
    bzm_dispatch_gate_init(&gate);
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&gate));
}

TEST_CASE("bzm dispatch authorization is checked on every call", "[bzm_dispatch_gate]")
{
    bool authorized = false;
    bzm_dispatch_gate_t gate;
    bzm_dispatch_gate_init(&gate);
    bzm_dispatch_gate_set(&gate, authorize_from_flag, &authorized);

    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&gate));
    authorized = true;
    TEST_ASSERT_TRUE(bzm_dispatch_gate_is_authorized(&gate));
    authorized = false;
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&gate));
}

TEST_CASE("bzm dispatch authorization can be explicitly closed", "[bzm_dispatch_gate]")
{
    bool authorized = true;
    bzm_dispatch_gate_t gate;
    bzm_dispatch_gate_init(&gate);
    bzm_dispatch_gate_set(&gate, authorize_from_flag, &authorized);
    TEST_ASSERT_TRUE(bzm_dispatch_gate_is_authorized(&gate));

    bzm_dispatch_gate_set(&gate, NULL, &authorized);
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&gate));
    TEST_ASSERT_NULL(gate.context);
}
