#include "bitmain_job_allocator_fault_injector.h"
#include <stdlib.h>
#include <string.h>

static size_t calls;
static size_t failure;

void bitmain_job_allocator_fault_injector_reset(size_t failure_at)
{
    calls = 0;
    failure = failure_at;
}

size_t bitmain_job_allocator_fault_injector_calls(void) { return calls; }

void *bitmain_job_allocator_fault_injector_malloc(size_t size)
{
    if (++calls == failure) return NULL;
    return malloc(size);
}

char *bitmain_job_allocator_fault_injector_strdup(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = bitmain_job_allocator_fault_injector_malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}
