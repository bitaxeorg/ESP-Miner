#include <sys/time.h>
#include <limits.h>

#include "work_queue.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "string.h"
#include "esp_timer.h"

#include "asic.h"
#include "system.h"
#include "esp_heap_caps.h"
#include "sv2_protocol.h"
#include "stratum_api.h"
#include "stratum_v2_task.h"
#include "utils.h"

static const char *TAG = "create_jobs_task";

static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty);
static void generate_work_sv2(GlobalState *GLOBAL_STATE, sv2_job_t *job, uint32_t difficulty);
static void generate_work_sv2_ext(GlobalState *GLOBAL_STATE, sv2_ext_job_t *job, uint32_t difficulty, uint64_t extranonce_2_counter);

// Free a work item using the correct free function for the protocol it was created under
static void free_work_item(GlobalState *GLOBAL_STATE, void *work, stratum_protocol_t protocol)
{
    if (!work) return;
    if (protocol == STRATUM_V2) {
        if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
            sv2_ext_job_free((sv2_ext_job_t *)work);
        } else {
            free(work);  // sv2_job_t is flat
        }
    } else {
        STRATUM_V1_free_mining_notify(work);
    }
}

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // Initialize ASIC task module (moved from ASIC_task)
    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = heap_caps_malloc(sizeof(bm_job *) * 128, MALLOC_CAP_SPIRAM);
    GLOBAL_STATE->valid_jobs = heap_caps_malloc(sizeof(uint8_t) * 128, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < 128; i++) {
        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }

    uint32_t difficulty = GLOBAL_STATE->pool_difficulty;
    void *current_work = NULL;
    stratum_protocol_t current_work_protocol = GLOBAL_STATE->stratum_protocol;
    uint64_t extranonce_2 = 0;
    int timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);

    ESP_LOGI(TAG, "ASIC Job Interval: %d ms", timeout_ms);
    ESP_LOGI(TAG, "ASIC Ready!");

    while (1) {
        // Read protocol dynamically each iteration (coordinator may have switched it)
        stratum_protocol_t active_protocol = GLOBAL_STATE->stratum_protocol;

        // If protocol changed, discard current_work (it belongs to the old protocol)
        // Always update current_work_protocol so the post-dequeue check doesn't
        // incorrectly discard the first valid work item from the new protocol.
        if (active_protocol != current_work_protocol) {
            if (current_work != NULL) {
                ESP_LOGI(TAG, "Protocol switched from %s to %s, discarding current work",
                         current_work_protocol == STRATUM_V2 ? "SV2" : "V1",
                         active_protocol == STRATUM_V2 ? "SV2" : "V1");
                free_work_item(GLOBAL_STATE, current_work, current_work_protocol);
                current_work = NULL;
            }
            current_work_protocol = active_protocol;
        }

        uint64_t start_time = esp_timer_get_time();
        void *new_work = queue_dequeue_timeout(&GLOBAL_STATE->stratum_queue, timeout_ms);
        timeout_ms -= (esp_timer_get_time() - start_time) / 1000;

        if (new_work != NULL) {
            active_protocol = GLOBAL_STATE->stratum_protocol;

            // Free previous work using the protocol it was created under
            free_work_item(GLOBAL_STATE, current_work, current_work_protocol);
            current_work = NULL;

            if (active_protocol != current_work_protocol) {
                // Protocol switched during our blocking dequeue.
                // The dequeued item may be from either the old or new protocol —
                // we cannot safely determine which type it is, so discard it.
                // free() is safe for both sv2_job_t (flat) and mining_notify (malloc'd;
                // internal strings leak but this is a rare protocol-switch event).
                ESP_LOGW(TAG, "Protocol switch detected during dequeue, discarding stale item");
                free(new_work);
                current_work_protocol = active_protocol;
                timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
                continue;
            }

            // Protocol unchanged — item matches current_work_protocol. Safe to cast.
            if (current_work_protocol == STRATUM_V2) {
                if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
                    ESP_LOGI(TAG, "New Work Dequeued SV2 ext job %lu", ((sv2_ext_job_t *)new_work)->job_id);
                } else {
                    ESP_LOGI(TAG, "New Work Dequeued SV2 job %lu", ((sv2_job_t *)new_work)->job_id);
                }
            } else {
                ESP_LOGI(TAG, "New Work Dequeued %s", ((mining_notify *)new_work)->job_id);
            }

            current_work = new_work;

            if (GLOBAL_STATE->new_set_mining_difficulty_msg) {
                ESP_LOGI(TAG, "New pool difficulty %lu", GLOBAL_STATE->pool_difficulty);
                difficulty = GLOBAL_STATE->pool_difficulty;
                GLOBAL_STATE->new_set_mining_difficulty_msg = false;
            }

            if (GLOBAL_STATE->new_stratum_version_rolling_msg && GLOBAL_STATE->ASIC_initalized) {
                ESP_LOGI(TAG, "Set chip version rolls %i", (int)(GLOBAL_STATE->version_mask >> 13));
                ASIC_set_version_mask(GLOBAL_STATE, GLOBAL_STATE->version_mask);
                GLOBAL_STATE->new_stratum_version_rolling_msg = false;
            }

            extranonce_2 = 0;

            // Check clean_jobs flag
            bool clean;
            if (current_work_protocol == STRATUM_V2) {
                if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
                    clean = ((sv2_ext_job_t *)current_work)->clean_jobs;
                } else {
                    clean = ((sv2_job_t *)current_work)->clean_jobs;
                }
            } else {
                clean = ((mining_notify *)current_work)->clean_jobs;
            }
            if (!clean) {
                continue;
            }
        } else {
            if (current_work == NULL) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
            // SV2 standard channel: the ASIC has enough nonce+version space
            // (2^32 nonces x version rolls) to keep mining without re-feeding.
            // Re-sending the same job restarts the nonce search from 0 and
            // produces duplicate shares. Only send work on new jobs.
            // (V1 and SV2 extended are fine — extranonce_2 gives unique work each time.)
            if (active_protocol == STRATUM_V2 && !stratum_v2_is_extended_channel(GLOBAL_STATE)) {
                timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
                continue;
            }
        }

        // Final protocol check before generating work — protocol may have switched
        // during a timeout dequeue while we still hold stale current_work
        active_protocol = GLOBAL_STATE->stratum_protocol;
        if (active_protocol != current_work_protocol) {
            free_work_item(GLOBAL_STATE, current_work, current_work_protocol);
            current_work = NULL;
            current_work_protocol = active_protocol;
            timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
            continue;
        }

        // Generate and send job
        if (active_protocol == STRATUM_V2) {
            if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
                generate_work_sv2_ext(GLOBAL_STATE, (sv2_ext_job_t *)current_work, difficulty, extranonce_2);
                extranonce_2++;
            } else {
                generate_work_sv2(GLOBAL_STATE, (sv2_job_t *)current_work, difficulty);
            }
        } else {
            generate_work(GLOBAL_STATE, (mining_notify *)current_work, extranonce_2, difficulty);
            extranonce_2++;
        }
        timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
    }
}

static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty)
{
    char extranonce_2_str[GLOBAL_STATE->extranonce_2_len * 2 + 1];
    extranonce_2_generate(extranonce_2, GLOBAL_STATE->extranonce_2_len, extranonce_2_str);

    uint8_t coinbase_tx_hash[32];
    calculate_coinbase_tx_hash(notification->coinbase_1, notification->coinbase_2, GLOBAL_STATE->extranonce_str, extranonce_2_str, coinbase_tx_hash);

    uint8_t merkle_root[32];
    calculate_merkle_root_hash(coinbase_tx_hash, (uint8_t(*)[32])notification->merkle_branches, notification->n_merkle_branches, merkle_root);

    bm_job *next_job = malloc(sizeof(bm_job));

    if (next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for new job");
        return;
    }

    construct_bm_job(notification, merkle_root, GLOBAL_STATE->version_mask, difficulty, next_job);

    next_job->extranonce2 = strdup(extranonce_2_str);
    next_job->jobid = strdup(notification->job_id);
    next_job->version_mask = GLOBAL_STATE->version_mask;

    // Check if ASIC is initialized before trying to send work
    if (!GLOBAL_STATE->ASIC_initalized) {
        ESP_LOGW(TAG, "ASIC not initialized, skipping job send");
        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);
        return;
    }

    ASIC_send_work(GLOBAL_STATE, next_job);
}

// Construct bm_job directly from SV2 fields (no coinbase/merkle computation needed).
// Standard channels rely on version rolling for unique work — the ASIC rolls the
// version bits using version_mask, giving different midstates per nonce search space.
static void generate_work_sv2(GlobalState *GLOBAL_STATE, sv2_job_t *sv2_job, uint32_t difficulty)
{
    bm_job *next_job = malloc(sizeof(bm_job));
    if (next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for new SV2 job");
        return;
    }

    uint32_t version_mask = GLOBAL_STATE->version_mask;

    next_job->version = sv2_job->version;
    next_job->target = sv2_job->nbits;
    next_job->ntime = sv2_job->ntime;
    next_job->starting_nonce = 0;
    next_job->pool_diff = difficulty;

    // SV2 provides merkle_root and prev_hash in internal byte order (SHA-256 output order).
    // For bm_job storage: apply reverse_32bit_words (same as construct_bm_job does)
    reverse_32bit_words(sv2_job->merkle_root, next_job->merkle_root);
    reverse_32bit_words(sv2_job->prev_hash, next_job->prev_block_hash);

    // Compute midstate(s) using the same logic as construct_bm_job.
    // Midstate covers bytes 0-63 of block header: version(4B) + prev_hash(32B) + merkle_root[0:28](28B).
    uint8_t midstate_data[64];
    uint32_t base_version = sv2_job->version;
    memcpy(midstate_data, &base_version, 4);
    memcpy(midstate_data + 4, sv2_job->prev_hash, 32);
    memcpy(midstate_data + 36, sv2_job->merkle_root, 28);

    uint8_t midstate[32];
    midstate_sha256_bin(midstate_data, 64, midstate);
    reverse_32bit_words(midstate, next_job->midstate);

    if (version_mask != 0) {
        uint32_t rolled_version = increment_bitmask(base_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, next_job->midstate1);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, next_job->midstate2);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, next_job->midstate3);
        next_job->num_midstates = 4;
    } else {
        next_job->num_midstates = 1;
    }

    // SV2 job metadata
    char jobid_str[16];
    snprintf(jobid_str, sizeof(jobid_str), "%lu", sv2_job->job_id);
    next_job->jobid = strdup(jobid_str);
    next_job->extranonce2 = strdup(""); // unused in SV2 standard
    next_job->version_mask = version_mask;

    if (!GLOBAL_STATE->ASIC_initalized) {
        ESP_LOGW(TAG, "ASIC not initialized, skipping SV2 job send");
        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);
        return;
    }

    ASIC_send_work(GLOBAL_STATE, next_job);
}

// Extended channel work generation: compute coinbase hash from prefix+extranonce+suffix,
// then merkle root from merkle path, then midstates. extranonce_2 provides unique work.
static void generate_work_sv2_ext(GlobalState *GLOBAL_STATE, sv2_ext_job_t *ext_job,
                                   uint32_t difficulty, uint64_t extranonce_2_counter)
{
    sv2_conn_t *conn = GLOBAL_STATE->sv2_conn;
    if (!conn) return;

    bm_job *next_job = malloc(sizeof(bm_job));
    if (!next_job) {
        ESP_LOGE(TAG, "Failed to allocate memory for SV2 ext job");
        return;
    }

    uint32_t version_mask = GLOBAL_STATE->version_mask;

    // Derive extranonce_2 from counter
    // SV2 spec: extranonce_size is the miner's rollable portion (not total)
    uint8_t extranonce_2_len = conn->extranonce_size;
    uint8_t extranonce_2[32];
    memset(extranonce_2, 0, sizeof(extranonce_2));
    // Encode counter as big-endian bytes
    for (int i = extranonce_2_len - 1; i >= 0 && extranonce_2_counter > 0; i--) {
        extranonce_2[i] = (uint8_t)(extranonce_2_counter & 0xFF);
        extranonce_2_counter >>= 8;
    }

    // Compute coinbase tx hash: prefix + extranonce_prefix + extranonce_2 + suffix
    uint8_t coinbase_tx_hash[32];
    calculate_coinbase_tx_hash_bin(
        ext_job->coinbase_prefix, ext_job->coinbase_prefix_len,
        conn->extranonce_prefix, conn->extranonce_prefix_len,
        extranonce_2, extranonce_2_len,
        ext_job->coinbase_suffix, ext_job->coinbase_suffix_len,
        coinbase_tx_hash);

    // Compute merkle root
    uint8_t merkle_root[32];
    calculate_merkle_root_hash(coinbase_tx_hash,
                               (const uint8_t (*)[32])ext_job->merkle_path,
                               ext_job->merkle_path_count, merkle_root);

    // Fill bm_job fields
    next_job->version = ext_job->version;
    next_job->target = ext_job->nbits;
    next_job->ntime = ext_job->ntime;  // no offset — extranonce provides uniqueness
    next_job->starting_nonce = 0;
    next_job->pool_diff = difficulty;

    // Same byte-order handling as generate_work_sv2
    reverse_32bit_words(merkle_root, next_job->merkle_root);
    reverse_32bit_words(ext_job->prev_hash, next_job->prev_block_hash);

    // Compute midstate(s)
    uint8_t midstate_data[64];
    uint32_t base_version = ext_job->version;
    memcpy(midstate_data, &base_version, 4);
    memcpy(midstate_data + 4, ext_job->prev_hash, 32);
    memcpy(midstate_data + 36, merkle_root, 28);

    uint8_t midstate[32];
    midstate_sha256_bin(midstate_data, 64, midstate);
    reverse_32bit_words(midstate, next_job->midstate);

    if (version_mask != 0) {
        uint32_t rolled_version = increment_bitmask(base_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, next_job->midstate1);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, next_job->midstate2);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, next_job->midstate3);
        next_job->num_midstates = 4;
    } else {
        next_job->num_midstates = 1;
    }

    // Job metadata
    char jobid_str[16];
    snprintf(jobid_str, sizeof(jobid_str), "%lu", ext_job->job_id);
    next_job->jobid = strdup(jobid_str);

    // Store extranonce_2 as hex for share submission
    char en2_hex[65];
    bin2hex(extranonce_2, extranonce_2_len, en2_hex, sizeof(en2_hex));
    next_job->extranonce2 = strdup(en2_hex);
    next_job->version_mask = version_mask;

    if (!GLOBAL_STATE->ASIC_initalized) {
        ESP_LOGW(TAG, "ASIC not initialized, skipping SV2 ext job send");
        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);
        return;
    }

    ASIC_send_work(GLOBAL_STATE, next_job);
}
