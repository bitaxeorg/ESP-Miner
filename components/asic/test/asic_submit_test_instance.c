#ifdef ESP_PLATFORM
#include "../../stratum/test/stubs/global_state.h"
#include "bitmain_job_test_bindings.h"
#define ASIC_send_job test_asic_send_job
#define ASIC_send_work spy_asic_send_work
#include "../asic_submit.c"
#endif
