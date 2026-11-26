#include "check.h"
#include "bzm/topology.h"

static unsigned calls;

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *function, void *caller)
{
    (void)function;
    (void)caller;
    ++calls;
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *function, void *caller)
{
    (void)function;
    (void)caller;
}

int main(void)
{
    calls = 0;
    for (uint16_t index = 0; index < BZM_TOPOLOGY_ACTIVATION_COUNT; ++index) {
        bzm_engine_location_t scheduled, physical;
        CHECK(bzm_topology_activation_at(index, BZM_ENGINE_STACK_BOTTOM, &scheduled));
        CHECK(bzm_topology_from_physical_id(scheduled.physical_id, &physical));
        CHECK(physical.topology_index == scheduled.topology_index);
    }
    printf("%u topology calls for %u activation and physical lookup pairs\n",
           calls, BZM_TOPOLOGY_ACTIVATION_COUNT);
    CHECK(calls <= 32 * BZM_TOPOLOGY_ACTIVATION_COUNT);
    return 0;
}
