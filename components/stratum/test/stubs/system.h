#ifndef TEST_STUB_SYSTEM_H
#define TEST_STUB_SYSTEM_H

#include <stdint.h>
#include "global_state.h"

void POOL_set_error(GlobalState *gs, uint16_t pool_idx, const char *err_msg);
void POOL_reset_session(GlobalState *gs, uint16_t pool_idx);
void POOL_clear_status(GlobalState *gs, uint16_t pool_idx);

#endif /* TEST_STUB_SYSTEM_H */
