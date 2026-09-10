```c
#include <sys/time.h>
#include <limits.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "miner_job.h"
#include "esp_timer.h"

#include "asic.h"
#include "system.h"
#include "esp_heap_caps.h"
#include "utils.h"

static const char *TAG = "create_jobs_task";

#define MAX_EXTRANONCE1_LEN 32
#define MAX_EXTRANONCE2_LEN 32

#define MAX_EXTRANONCE1_STR \
    (MAX_EXTRANONCE1_LEN * 2 + 1)

#define MAX_EXTRANONCE2_STR \
    (MAX_EXTRANONCE2_LEN * 2 + 1)


/*
 * ============================================================
 * EXTRANONCE1 INITIAL ROLL
 * ============================================================
 *
 * Pool:
 *
 *   20 04 53 83
 *
 * becomes:
 *
 *   00 04 53 83
 *
 *
 * Pool:
 *
 *   30 49 87 65
 *
 * becomes:
 *
 *   10 49 87 65
 *
 *
 * ONLY THE FIRST HEX NIBBLE IS CHANGED.
 *
 * 2 -> 0
 * 3 -> 1
 *
 * The remaining 7 hexadecimal digits remain unchanged.
 */
static bool extranonce1_make_initial_roll(
    const uint8_t *src,
    uint8_t *dst,
    size_t len)
{
    if (src == NULL || dst == NULL || len == 0) {
        return false;
    }

    memcpy(dst, src, len);

    uint8_t first_nibble =
        (dst[0] >> 4) & 0x0F;

    if (first_nibble == 0x02) {

        /*
         * 2xxxxxxx -> 0xxxxxxx
         */
        dst[0] &= 0x0F;

    }
    else if (first_nibble == 0x03) {

        /*
         * 3xxxxxxx -> 1xxxxxxx
         */
        dst[0] &= 0x0F;
        dst[0] |= 0x10;

    }
    else {

        /*
         * We only roll extranonce1 values beginning
         * with 2 or 3 according to the requested rule.
         *
         * Do not modify anything else.
         */
        ESP_LOGW(
            TAG,
            "Unsupported extranonce1 first nibble: %u",
            first_nibble
        );

        return false;
    }

    return true;
}


/*
 * ============================================================
 * EXTRANONCE1 ROLL NEXT
 * ============================================================
 *
 * Example:
 *
 *   00045383
 *   00045384
 *   00045385
 *   ...
 *
 * or:
 *
 *   10498765
 *   10498766
 *   10498767
 *   ...
 *
 *
 * IMPORTANT:
 *
 * The FIRST HEX NIBBLE is NEVER changed here.
 *
 * So:
 *
 *   0xxxxxxx stays 0xxxxxxx
 *
 * or:
 *
 *   1xxxxxxx stays 1xxxxxxx
 *
 *
 * We increment the LOWER 7 hexadecimal digits.
 *
 * Example:
 *
 *   0FFFFFFF
 *
 * becomes:
 *
 *   00000000
 *
 * because the 28-bit rolling area overflowed.
 *
 *
 *   1FFFFFFF
 *
 * becomes:
 *
 *   10000000
 */
static void extranonce1_roll_next(
    uint8_t *value,
    size_t len)
{
    if (value == NULL || len == 0) {
        return;
    }

    /*
     * Preserve the first hexadecimal nibble.
     *
     * 0xxxxxxx -> prefix 0
     * 1xxxxxxx -> prefix 1
     */
    uint8_t prefix =
        value[0] & 0xF0;


    /*
     * Clear the first nibble temporarily.
     *
     * This gives us the 28-bit rolling area.
     */
    value[0] &= 0x0F;


    /*
     * Increment from the LAST byte.
     *
     * This treats the hexadecimal representation as:
     *
     *   00 04 53 83
     *
     * -> 00 04 53 84
     *
     * which corresponds to:
     *
     *   00045383
     *   00045384
     */
    for (int i = (int)len - 1; i >= 0; i--) {

        value[i]++;

        if (value[i] != 0x00) {
            /*
             * No carry.
             */
            break;
        }
    }


    /*
     * Restore original prefix.
     *
     * Only 0x00 or 0x10 should normally be here.
     */
    value[0] &= 0x0F;
    value[0] |= prefix;
}


/*
 * ============================================================
 * HEX DEBUG HELPER
 * ============================================================
 */
static void extranonce_to_hex(
    const uint8_t *data,
    size_t len,
    char *out,
    size_t out_size)
{
    if (data == NULL ||
        out == NULL ||
        out_size == 0) {

        return;
    }

    if ((len * 2 + 1) > out_size) {
        out[0] = '\0';
        return;
    }

    bin2hex(
        data,
        len,
        out,
        out_size
    );
}


/*
 * ============================================================
 * GENERATE WORK
 * ============================================================
 */
static void generate_work_from_miner_job(
    GlobalState *GLOBAL_STATE,
    const miner_job_t *job,
    const uint8_t *rolled_extranonce1,
    size_t rolled_extranonce1_len,
    uint32_t current_version)
{
    if (GLOBAL_STATE == NULL || job == NULL) {
        return;
    }


    bm_job *next_job =
        malloc(sizeof(bm_job));

    if (next_job == NULL) {

        ESP_LOGE(
            TAG,
            "Failed to allocate memory for new job"
        );

        return;
    }


    uint32_t version_mask =
        job->version_mask;

    double job_diff =
        job->pool_diff;


    uint8_t merkle_root[32] = {0};


    /*
     * --------------------------------------------------------
     * EXTRANONCE2
     * --------------------------------------------------------
     *
     * ALWAYS ZERO.
     *
     * We do NOT receive it as a rolling value anymore.
     */
    uint8_t extranonce_2_bin[MAX_EXTRANONCE2_LEN] = {0};

    char extranonce_2_str[MAX_EXTRANONCE2_STR] = "";


    size_t e2_len = 0;


    /*
     * Version logic remains compatible with original code.
     */
    uint32_t effective_version =
        job->version;


    if (!GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling &&
        !miner_job_is_rollable(job)) {

        effective_version =
            current_version;
    }


    /*
     * ========================================================
     * SV2 STANDARD
     * ========================================================
     */
    if (job->type == JOB_TYPE_SV2_STANDARD) {

        memcpy(
            merkle_root,
            job->merkle_root,
            32
        );

    }
    else {

        /*
         * ----------------------------------------------------
         * Check extranonce2 length
         * ----------------------------------------------------
         */
        e2_len =
            job->extranonce2_len;


        if (e2_len > MAX_EXTRANONCE2_LEN) {

            ESP_LOGE(
                TAG,
                "extranonce2_len %u exceeds maximum %d",
                (unsigned)e2_len,
                MAX_EXTRANONCE2_LEN
            );

            free(next_job);
            return;
        }


        /*
         * extranonce2 is ZERO.
         *
         * No matter how many times we call this function,
         * this remains:
         *
         *   00000000
         *
         * for a 4-byte extranonce2.
         */
        if (e2_len > 0) {

            bin2hex(
                extranonce_2_bin,
                e2_len,
                extranonce_2_str,
                sizeof(extranonce_2_str)
            );
        }


        /*
         * ----------------------------------------------------
         * Validate rolled extranonce1
         * ----------------------------------------------------
         */
        if (rolled_extranonce1 == NULL ||
            rolled_extranonce1_len != job->extranonce1_len) {

            ESP_LOGE(
                TAG,
                "Invalid rolled extranonce1"
            );

            free(next_job);
            return;
        }


        if (rolled_extranonce1_len >
            MAX_EXTRANONCE1_LEN) {

            ESP_LOGE(
                TAG,
                "extranonce1_len %u exceeds maximum %d",
                (unsigned)rolled_extranonce1_len,
                MAX_EXTRANONCE1_LEN
            );

            free(next_job);
            return;
        }


        /*
         * ----------------------------------------------------
         * DEBUG
         * ----------------------------------------------------
         */
        char rolled_extranonce1_str[
            MAX_EXTRANONCE1_STR
        ] = "";


        extranonce_to_hex(
            rolled_extranonce1,
            rolled_extranonce1_len,
            rolled_extranonce1_str,
            sizeof(rolled_extranonce1_str)
        );


        ESP_LOGI(
            TAG,
            "Rolling work: extranonce1=%s extranonce2=%s",
            rolled_extranonce1_str,
            extranonce_2_str
        );


        /*
         * ====================================================
         * COINBASE HASH
         * ====================================================
         *
         * Coinbase:
         *
         *   prefix
         *   +
         *   ROLLED EXTRANONCE1
         *   +
         *   ZERO EXTRANONCE2
         *   +
         *   suffix
         */
        uint8_t coinbase_tx_hash[32];


        calculate_coinbase_tx_hash_bin(
            job->coinbase_prefix,
            job->coinbase_prefix_len,

            /*
             * ROLLED EXTRANONCE1
             */
            rolled_extranonce1,
            rolled_extranonce1_len,

            /*
             * ALWAYS ZERO EXTRANONCE2
             */
            extranonce_2_bin,
            e2_len,

            job->coinbase_suffix,
            job->coinbase_suffix_len,

            coinbase_tx_hash
        );


        /*
         * ----------------------------------------------------
         * MERKLE ROOT
         * ----------------------------------------------------
         */
        calculate_merkle_root_hash(
            coinbase_tx_hash,

            (const uint8_t (*)[32])
                job->merkle_path,

            job->merkle_path_count,

            merkle_root
        );
    }


    /*
     * ========================================================
     * CONSTRUCT BM JOB
     * ========================================================
     */
    construct_bm_job_from_miner_job(
        job,
        effective_version,
        merkle_root,
        version_mask,
        job_diff,
        GLOBAL_STATE->DEVICE_CONFIG.family.asic.software_midstates,
        next_job
    );


    /*
     * Job ID
     */
    next_job->jobid =
        strdup(job->job_id);


    /*
     * ========================================================
     * EXTRANONCE2 SENT TO ASIC
     * ========================================================
     *
     * ALWAYS ZERO.
     *
     * 4 bytes:
     *
     *   00000000
     */
    next_job->extranonce2 =
        strdup(extranonce_2_str);


    if (next_job->jobid == NULL ||
        next_job->extranonce2 == NULL) {

        ESP_LOGE(
            TAG,
            "Failed to allocate job metadata"
        );

        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);

        return;
    }


    /*
     * ========================================================
     * ASIC READY?
     * ========================================================
     */
    if (!GLOBAL_STATE->ASIC_initalized) {

        ESP_LOGW(
            TAG,
            "ASIC not initialized, skipping job send"
        );

        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);

        return;
    }


    /*
     * ========================================================
     * SEND TO ASIC
     * ========================================================
     */
    ASIC_send_work(
        GLOBAL_STATE,
        next_job
    );
}


/*
 * ============================================================
 * CREATE JOBS TASK
 * ============================================================
 */
void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE =
        (GlobalState *)pvParameters;


    /*
     * --------------------------------------------------------
     * CURRENT JOB
     * --------------------------------------------------------
     */
    miner_job_t *current_work = NULL;

    bool current_work_sent = false;


    /*
     * --------------------------------------------------------
     * ROLLING EXTRANONCE1
     * --------------------------------------------------------
     *
     * This is the important new state.
     *
     * It survives between ASIC work generations.
     */
    uint8_t rolling_extranonce1[
        MAX_EXTRANONCE1_LEN
    ] = {0};


    size_t rolling_extranonce1_len = 0;


    /*
     * --------------------------------------------------------
     * EXTRANONCE2
     * --------------------------------------------------------
     *
     * Kept only for compatibility.
     *
     * IT IS ALWAYS ZERO.
     *
     * NEVER increment this.
     */
    uint64_t extranonce_2 = 0;


    /*
     * --------------------------------------------------------
     * VERSION
     * --------------------------------------------------------
     */
    uint32_t current_version_mask = 0;

    uint32_t current_version = 0;


    /*
     * --------------------------------------------------------
     * ASIC FREQUENCY
     * --------------------------------------------------------
     */
    int timeout_ms =
        ASIC_get_asic_job_frequency_ms(
            GLOBAL_STATE
        );


    ESP_LOGI(
        TAG,
        "ASIC Job Interval: %d ms",
        timeout_ms
    );


    ESP_LOGI(
        TAG,
        "ASIC Ready!"
    );


    /*
     * ========================================================
     * MAIN LOOP
     * ========================================================
     */
    while (1) {

        uint64_t start_time =
            esp_timer_get_time();


        uint32_t slot_notify = 0;


        TickType_t wait_ticks =
            (timeout_ms > 0)
                ? pdMS_TO_TICKS(timeout_ms)
                : 0;


        BaseType_t notified =
            xTaskNotifyWait(
                0,
                ULONG_MAX,
                &slot_notify,
                wait_ticks
            );


        timeout_ms -=
            (esp_timer_get_time() -
             start_time) / 1000;


        /*
         * ====================================================
         * NEW POOL JOB
         * ====================================================
         */
        if (notified == pdTRUE) {

            miner_job_t *new_work =
                miner_job_get_slot(
                    (size_t)slot_notify
                );


            if (new_work == NULL) {

                ESP_LOGE(
                    TAG,
                    "miner_job_get_slot returned NULL"
                );

                continue;
            }


            ESP_LOGI(
                TAG,
                "New Work Activated (slot %lu) %s (type %d)",
                (unsigned long)slot_notify,
                new_work->job_id,
                new_work->type
            );


            /*
             * ------------------------------------------------
             * Activate new job
             * ------------------------------------------------
             */
            current_work =
                new_work;


            GLOBAL_STATE->active_job_slot_idx =
                (uint8_t)(
                    slot_notify %
                    MINER_JOB_POOL_SIZE
                );


            current_work_sent =
                false;


            current_version =
                new_work->version;


            /*
             * ------------------------------------------------
             * Version mask
             * ------------------------------------------------
             */
            if (new_work->version_mask !=
                    current_version_mask &&
                GLOBAL_STATE->ASIC_initalized) {

                ESP_LOGI(
                    TAG,
                    "Set chip version rolls %i",
                    (int)(
                        new_work->version_mask >> 13
                    )
                );


                ASIC_set_version_mask(
                    GLOBAL_STATE,
                    new_work->version_mask
                );


                current_version_mask =
                    new_work->version_mask;
            }


            /*
             * =================================================
             * RESET EXTRANONCE2
             * =================================================
             *
             * ALWAYS ZERO.
             */
            extranonce_2 = 0;


            /*
             * =================================================
             * INITIALIZE EXTRANONCE1 ROLLING
             * =================================================
             */
            if (new_work->type != JOB_TYPE_SV2_STANDARD) {

                if (new_work->extranonce1_len == 0) {

                    ESP_LOGW(
                        TAG,
                        "New job has zero extranonce1 length"
                    );

                    rolling_extranonce1_len = 0;

                }
                else if (
                    new_work->extranonce1_len >
                    MAX_EXTRANONCE1_LEN) {

                    ESP_LOGE(
                        TAG,
                        "extranonce1_len %u exceeds maximum %d",
                        (unsigned)new_work->extranonce1_len,
                        MAX_EXTRANONCE1_LEN
                    );

                    rolling_extranonce1_len = 0;

                }
                else {

                    rolling_extranonce1_len =
                        new_work->extranonce1_len;


                    /*
                     * Pool value:
                     *
                     * 20045383
                     *
                     * becomes:
                     *
                     * 00045383
                     */
                    bool roll_ok =
                        extranonce1_make_initial_roll(
                            new_work->extranonce1,
                            rolling_extranonce1,
                            rolling_extranonce1_len
                        );


                    if (!roll_ok) {

                        /*
                         * For values not beginning with
                         * 2 or 3 we keep the original value
                         * rather than corrupting it.
                         */
                        memcpy(
                            rolling_extranonce1,
                            new_work->extranonce1,
                            rolling_extranonce1_len
                        );


                        ESP_LOGW(
                            TAG,
                            "Initial extranonce1 roll not applied"
                        );
                    }


                    /*
                     * Debug
                     */
                    char pool_e1[
                        MAX_EXTRANONCE1_STR
                    ] = "";


                    char rolled_e1[
                        MAX_EXTRANONCE1_STR
                    ] = "";


                    extranonce_to_hex(
                        new_work->extranonce1,
                        new_work->extranonce1_len,
                        pool_e1,
                        sizeof(pool_e1)
                    );


                    extranonce_to_hex(
                        rolling_extranonce1,
                        rolling_extranonce1_len,
                        rolled_e1,
                        sizeof(rolled_e1)
                    );


                    ESP_LOGI(
                        TAG,
                        "EX1 START: pool=%s -> rolling=%s",
                        pool_e1,
                        rolled_e1
                    );
                }
            }
            else {

                /*
                 * SV2 Standard does not use the same
                 * extranonce1/coinbase path.
                 */
                rolling_extranonce1_len = 0;
            }


            /*
             * ------------------------------------------------
             * Staged job
             * ------------------------------------------------
             */
            if (!current_work->clean_jobs) {

                /*
                 * Staged job for next cycle.
                 * Let current ASIC cycle finish.
                 */
                continue;
            }
        }
        else {

            /*
             * =================================================
             * NO NEW POOL JOB
             * =================================================
             */
            if (current_work == NULL) {

                vTaskDelay(
                    100 / portTICK_PERIOD_MS
                );

                continue;
            }


            /*
             * Original hardware-version-rolling condition.
             */
            if (!miner_job_is_rollable(current_work) &&
                current_work_sent &&
                GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling) {

                timeout_ms =
                    ASIC_get_asic_job_frequency_ms(
                        GLOBAL_STATE
                    );

                continue;
            }
        }


        /*
         * ====================================================
         * GENERATE CURRENT WORK
         * ====================================================
         *
         * On the first iteration:
         *
         *   20045383 -> 00045383
         *
         * Next iteration:
         *
         *   00045384
         *
         * Next:
         *
         *   00045385
         *
         * etc.
         */
        if (current_work->type ==
                JOB_TYPE_SV2_STANDARD) {

            /*
             * Standard SV2 job.
             */
            generate_work_from_miner_job(
                GLOBAL_STATE,
                current_work,
                NULL,
                0,
                current_version
            );

        }
        else {

            /*
             * Normal Stratum-style job.
             */
            generate_work_from_miner_job(
                GLOBAL_STATE,
                current_work,
                rolling_extranonce1,
                rolling_extranonce1_len,
                current_version
            );
        }


        /*
         * ====================================================
         * COINBASE APPLY
         * ====================================================
         *
         * Keep original behavior:
         * only execute once for a newly activated job.
         */
        if (!current_work_sent) {

            SYSTEM_decode_and_apply_coinbase(
                GLOBAL_STATE,
                current_work
            );
        }


        current_work_sent = true;


        /*
         * ====================================================
         * ADVANCE EXTRANONCE1
         * ====================================================
         *
         * IMPORTANT:
         *
         * This happens AFTER the current work has been sent.
         *
         * Therefore:
         *
         * Work #1:
         *   00045383
         *
         * Work #2:
         *   00045384
         *
         * Work #3:
         *   00045385
         *
         * etc.
         *
         * extranonce2 NEVER changes.
         */
        if (current_work->type !=
                JOB_TYPE_SV2_STANDARD &&
            rolling_extranonce1_len > 0) {

            extranonce1_roll_next(
                rolling_extranonce1,
                rolling_extranonce1_len
            );


            /*
             * Debug next value.
             */
            char next_e1[
                MAX_EXTRANONCE1_STR
            ] = "";


            extranonce_to_hex(
                rolling_extranonce1,
                rolling_extranonce1_len,
                next_e1,
                sizeof(next_e1)
            );


            ESP_LOGD(
                TAG,
                "Next extranonce1=%s",
                next_e1
            );
        }


        /*
         * ====================================================
         * SOFTWARE VERSION ROLLING
         * ====================================================
         *
         * Keep original behavior for ASICs without hardware
         * version rolling.
         */
        if (!miner_job_is_rollable(current_work) &&
            !GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling) {

            uint32_t mask =
                (current_work->version_mask != 0)
                    ? current_work->version_mask
                    : BIP320_VERSION_ROLLING_MASK;


            uint8_t midstates =
                GLOBAL_STATE->DEVICE_CONFIG.family.asic.software_midstates;


            for (int i = 0; i < midstates; i++) {

                current_version =
                    increment_bitmask(
                        current_version,
                        mask
                    );
            }
        }


        /*
         * ====================================================
         * RESET TIMER
         * ====================================================
         */
        timeout_ms =
            ASIC_get_asic_job_frequency_ms(
                GLOBAL_STATE
            );
    }
}
```
