#include "bzm/dispatch_gate.h"

#include <stddef.h>

void bzm_dispatch_gate_init(bzm_dispatch_gate_t * gate)
{
    if (gate != NULL) {
        *gate = (bzm_dispatch_gate_t){0};
    }
}

void bzm_dispatch_gate_set(bzm_dispatch_gate_t * gate, bzm_dispatch_authorizer_t authorize, void * context)
{
    if (gate == NULL)
        return;
    gate->authorize = authorize;
    gate->context = authorize == NULL ? NULL : context;
}

bool bzm_dispatch_gate_is_authorized(const bzm_dispatch_gate_t * gate)
{
    return gate != NULL && gate->authorize != NULL && gate->authorize(gate->context);
}
