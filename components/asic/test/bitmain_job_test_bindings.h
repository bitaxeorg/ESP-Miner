#ifndef BITMAIN_JOB_FIXTURE_BINDINGS_H_
#define BITMAIN_JOB_FIXTURE_BINDINGS_H_

#include <stdlib.h>
#include <string.h>
#include "bitmain_job_allocator_fault_injector.h"

#ifdef ESP_PLATFORM
#define bm_job_build_from_asic_job test_bm_job_build_from_asic_job
#endif
#define malloc(size) bitmain_job_allocator_fault_injector_malloc(size)
#define strdup(text) bitmain_job_allocator_fault_injector_strdup(text)

#endif
