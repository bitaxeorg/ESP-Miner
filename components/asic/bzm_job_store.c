#include "bzm/job_store.h"

#include "esp_heap_caps.h"

#include <string.h>

static void clear_entry(bzm_job_store_entry_t *entry)
{
    if (entry == NULL) return;
    memset(entry, 0, sizeof(*entry));
}

bool bzm_job_store_init(bzm_job_store_t *store)
{
    return bzm_job_store_init_with_caps(store, MALLOC_CAP_DEFAULT);
}

bool bzm_job_store_init_with_caps(bzm_job_store_t *store,
                                   uint32_t memory_caps)
{
    if (store == NULL) return false;
    memset(store, 0, sizeof(*store));
    store->entries = heap_caps_calloc(
        BZM_JOB_STORE_CAPACITY, sizeof(*store->entries), memory_caps);
    if (store->entries == NULL) return false;
    store->capacity = BZM_JOB_STORE_CAPACITY;
    store->next_generation = 1;
    if (pthread_mutex_init(&store->lock, NULL) != 0) {
        heap_caps_free(store->entries);
        memset(store, 0, sizeof(*store));
        return false;
    }
    return true;
}

void bzm_job_store_destroy(bzm_job_store_t *store)
{
    if (store == NULL) return;
    if (store->entries == NULL) {
        memset(store, 0, sizeof(*store));
        return;
    }
    bzm_job_store_invalidate_all(store);
    pthread_mutex_destroy(&store->lock);
    heap_caps_free(store->entries);
    memset(store, 0, sizeof(*store));
}

static bool store_entry(bzm_job_store_t *store, uint8_t slot,
                        bzm_work_handle_t handle,
                        const asic_job_t *template)
{
    bzm_job_store_entry_t replacement = {
        .valid = true,
        .handle = handle,
    };
    replacement.template = *template;

    clear_entry(&store->entries[slot]);
    store->entries[slot] = replacement;
    return true;
}

bool bzm_job_store_store_generated(bzm_job_store_t *store,
                                    const asic_job_t *template,
                                    bzm_work_handle_t *handle)
{
    if (store == NULL || store->entries == NULL || store->capacity == 0 ||
        template == NULL) return false;

    pthread_mutex_lock(&store->lock);
    uint8_t slot = (uint8_t)(store->next_slot++ % store->capacity);
    uint64_t generation = store->next_generation++;
    if (store->next_generation == 0) store->next_generation = 1;
    bzm_work_handle_t generated = (generation << 8) | slot;
    bool stored = store_entry(store, slot, generated, template);
    pthread_mutex_unlock(&store->lock);
    if (stored && handle != NULL) *handle = generated;
    return stored;
}

bool bzm_job_store_snapshot(bzm_job_store_t *store,
                            bzm_work_handle_t handle,
                            asic_job_t *snapshot)
{
    if (store == NULL || store->entries == NULL || snapshot == NULL ||
        handle == BZM_WORK_HANDLE_INVALID) {
        return false;
    }

    uint8_t slot = (uint8_t)(handle & 0xff);
    if (slot >= store->capacity) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    pthread_mutex_lock(&store->lock);
    bzm_job_store_entry_t *entry = &store->entries[slot];
    bool found = entry->valid && entry->handle == handle;
    if (found) {
        *snapshot = entry->template;
    }
    pthread_mutex_unlock(&store->lock);
    return found;
}

bool bzm_job_store_contains(bzm_job_store_t *store,
                             bzm_work_handle_t handle)
{
    if (store == NULL || store->entries == NULL ||
        handle == BZM_WORK_HANDLE_INVALID) return false;
    uint8_t slot = (uint8_t)(handle & 0xff);
    if (slot >= store->capacity) return false;
    pthread_mutex_lock(&store->lock);
    const bzm_job_store_entry_t *entry = &store->entries[slot];
    bool found = entry->valid && entry->handle == handle;
    pthread_mutex_unlock(&store->lock);
    return found;
}

bool bzm_job_store_release(bzm_job_store_t *store,
                            bzm_work_handle_t handle)
{
    if (store == NULL || store->entries == NULL ||
        handle == BZM_WORK_HANDLE_INVALID) return false;
    uint8_t slot = (uint8_t)(handle & 0xff);
    if (slot >= store->capacity) return false;
    pthread_mutex_lock(&store->lock);
    bzm_job_store_entry_t *entry = &store->entries[slot];
    bool released = entry->valid && entry->handle == handle;
    if (released) clear_entry(entry);
    pthread_mutex_unlock(&store->lock);
    return released;
}

void bzm_job_store_invalidate_all(bzm_job_store_t *store)
{
    if (store == NULL || store->entries == NULL) return;
    pthread_mutex_lock(&store->lock);
    for (size_t i = 0; i < store->capacity; ++i) {
        clear_entry(&store->entries[i]);
    }
    store->next_slot = 0;
    if (++store->next_generation == 0) store->next_generation = 1;
    pthread_mutex_unlock(&store->lock);
}
