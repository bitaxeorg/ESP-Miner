#include "cjson_allocator.h"

#include <stdint.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "sdkconfig.h"

static void *cjson_malloc_aligned(size_t size)
{
    const size_t alignment = _Alignof(cJSON);

    // cJSON also allocates strings; aligned_alloc requires a size multiple.
    if (size > SIZE_MAX - (alignment - 1)) {
        return NULL;
    }
    size = (size + alignment - 1) & ~(alignment - 1);

#ifdef CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_SPIRAM);
    }
#endif
    return aligned_alloc(alignment, size);
}

void cjson_allocator_init(void)
{
    cJSON_Hooks hooks = {
        .malloc_fn = cjson_malloc_aligned,
        .free_fn = free,
    };
    cJSON_InitHooks(&hooks);
}
