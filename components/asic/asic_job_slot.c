#include "asic_job_slot.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "asic_job_slot";

void asic_job_store_init(asic_job_store_t *store, asic_job_slot_entry_t *entries, uint16_t capacity)
{
    pthread_mutex_init(&store->lock, NULL);
    store->entries = entries;
    store->capacity = capacity;
    store->current_slot = 0;
    if (entries != NULL && capacity > 0) {
        memset(entries, 0, sizeof(entries[0]) * capacity);
    }
}

void asic_job_store_destroy(asic_job_store_t *store)
{
    if (store != NULL) {
        pthread_mutex_destroy(&store->lock);
    }
}

uint16_t asic_job_store_claim(asic_job_store_t *store, const asic_job_t *job)
{
    if (store == NULL || store->entries == NULL || store->capacity == 0) {
        return 0;
    }
    pthread_mutex_lock(&store->lock);
    store->current_slot = (store->current_slot + 1) % store->capacity;
    uint16_t slot = store->current_slot;

    if (job != NULL) {
        store->entries[slot].job = *job;
    } else {
        memset(&store->entries[slot].job, 0, sizeof(store->entries[slot].job));
    }
    store->entries[slot].is_valid = true;
    pthread_mutex_unlock(&store->lock);

    return slot;
}

bool asic_job_store_lookup(asic_job_store_t *store, uint16_t slot, asic_job_t *out_job)
{
    if (store == NULL || store->entries == NULL) {
        return false;
    }
    pthread_mutex_lock(&store->lock);
    if (slot >= store->capacity || !store->entries[slot].is_valid)
    {
        pthread_mutex_unlock(&store->lock);
        ESP_LOGW(TAG, "Invalid job nonce found, slot 0x%02X", slot);
        return false;
    }

    if (out_job != NULL) {
        *out_job = store->entries[slot].job;
    }
    pthread_mutex_unlock(&store->lock);

    return true;
}

void asic_job_store_invalidate_all(asic_job_store_t *store)
{
    if (store == NULL || store->entries == NULL) {
        return;
    }
    pthread_mutex_lock(&store->lock);
    for (size_t i = 0; i < store->capacity; ++i) {
        store->entries[i].is_valid = false;
    }
    pthread_mutex_unlock(&store->lock);
}
