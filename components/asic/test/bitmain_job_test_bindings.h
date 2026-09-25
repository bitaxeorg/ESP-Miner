#ifndef BITMAIN_JOB_TEST_BINDINGS_H_
#define BITMAIN_JOB_TEST_BINDINGS_H_

#include <stdlib.h>
#include <string.h>
#include "bitmain_job_allocator_fault_injector.h"

#define malloc(size) bitmain_job_allocator_fault_injector_malloc(size)

#endif
