#include "yyjson_utils.h"
#include <esp_heap_caps.h>

static void *psram_malloc(void *ctx, size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

static void *psram_realloc(void *ctx, void *ptr, size_t old_size, size_t size)
{
    void *new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!new_ptr && size > 0) {
        new_ptr = realloc(ptr, size);
    }
    return new_ptr;
}

static void psram_free(void *ctx, void *ptr)
{
    heap_caps_free(ptr);
}

static const yyjson_alc s_yyjson_psram_alc = {
    .malloc = psram_malloc,
    .realloc = psram_realloc,
    .free = psram_free,
    .ctx = NULL,
};

const yyjson_alc *yyjson_psram_alc(void)
{
    return &s_yyjson_psram_alc;
}

void yyjson_alc_free(const yyjson_alc *alc, void *ptr)
{
    if (!ptr) {
        return;
    }
    if (alc && alc->free) {
        alc->free(alc->ctx, ptr);
    } else {
        free(ptr);
    }
}

yyjson_mut_val *yyjson_diff_objects(yyjson_mut_doc *doc, yyjson_mut_val *old_obj, yyjson_mut_val *new_obj)
{
    if (!new_obj || !yyjson_mut_is_obj(new_obj)) return NULL;
    if (!old_obj || !yyjson_mut_is_obj(old_obj)) {
        return yyjson_mut_val_mut_copy(doc, new_obj);
    }

    yyjson_mut_val *diff = NULL;
    size_t idx, max;
    yyjson_mut_val *key, *new_val;
    yyjson_mut_obj_foreach(new_obj, idx, max, key, new_val) {
        yyjson_mut_val *old_val = yyjson_mut_obj_getn(old_obj, yyjson_mut_get_str(key), yyjson_mut_get_len(key));
        if (!old_val || !yyjson_mut_equals(old_val, new_val)) {
            if (yyjson_mut_is_obj(new_val) && old_val && yyjson_mut_is_obj(old_val)) {
                yyjson_mut_val *sub_diff = yyjson_diff_objects(doc, old_val, new_val);
                if (sub_diff) {
                    if (!diff) diff = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add(diff, yyjson_mut_val_mut_copy(doc, key), sub_diff);
                }
            } else {
                if (!diff) diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add(diff, yyjson_mut_val_mut_copy(doc, key), yyjson_mut_val_mut_copy(doc, new_val));
            }
        }
    }
    return diff;
}
