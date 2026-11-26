#ifndef BZM_DISPATCH_GATE_H
#define BZM_DISPATCH_GATE_H

#include <stdbool.h>

#include "bzm/board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bzm_dispatch_authorizer_t authorize;
    void * context;
} bzm_dispatch_gate_t;

void bzm_dispatch_gate_init(bzm_dispatch_gate_t * gate);
void bzm_dispatch_gate_set(bzm_dispatch_gate_t * gate, bzm_dispatch_authorizer_t authorize, void * context);
bool bzm_dispatch_gate_is_authorized(const bzm_dispatch_gate_t * gate);

#ifdef __cplusplus
}
#endif

#endif // BZM_DISPATCH_GATE_H
