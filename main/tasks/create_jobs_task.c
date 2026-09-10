```c
#include <sys/time.h>
#include <limits.h>
#include <inttypes.h>

#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "miner_job.h"
#include "string.h"
#include "esp_timer.h"

#include "asic.h"
#include "system.h"
#include "esp_heap_caps.h"
#include "utils.h"

static const char *TAG = "create_jobs_task";

#define MAX_EXTRANONCE2_LEN 32
#define MAX_EXTRANONCE2_STR (MAX_EXTRANONCE2_LEN * 2 + 1)

/*
 * extranonce1 rolling
 *
 * Examples:
 *
 *   20045383 -> 00045383
 *   30498765 -> 10498765
 *   2ABCDEF0 -> 0ABCDEF0
 *   3ABCDEF0 -> 1ABCDEF0
 *
 * Only the FIRST hexadecimal nibble is changed.
 *
 * 2 -> 0
 * 3 -> 1
 *
 * Everything else remains unchanged.
 */
static void roll_extranonce1(const uint8_t *src,
                             uint8_t *dst,
                             size_t len)
{
    if (src == NULL || dst == NULL || len == 0) {
        return;
    }

    /*
     * Copy the complete extranonce1 first.
     * This guarantees that all bytes except the first nibble
     * remain exactly the same.
     */
    memcpy(dst, src, len);

    /*
     * First byte contains two hexadecimal nibbles:
     *
     * 0x20 -> first nibble = 2
     * 0x30 -> first nibble = 3
     */
    uint8_t first_nibble = (dst[0] >> 4) & 0x0F;

    if (first_nibble == 0x02) {
        /*
         * 2xxxxxxx -> 0xxxxxxx
         *
         * Clear the high nibble.
         */
        dst[0] &= 0x0F;
    }
    else if (first_nibble == 0x03) {
        /*
         * 3xxxxxxx -> 1xxxxxxx
         *
         * Clear high nibble first,
         * then set it to 1.
         */
        dst[0] &= 0x0F;
        dst[0] |= 0x10;
    }
}


/*
 * Convert binary extranonce1 to hexadecimal string.
 *
 * This is only used for logging/debugging if needed.
 */
static void extranonce1_to_hex(const uint8_t *data,
                               size_t len,
                               char *out,
                               size_t out_size)
{
    if (data == NULL || out == NULL || out_size == 0) {
        return;
    }

    if ((len * 2 + 1) > out_size) {
        out[0] = '\0';
        return;
    }

    bin2hex(data, len, out, out_size);
}


static void generate_work_from_miner_job(
    GlobalState *GLOBAL_STATE,
    const miner_job_t *job,
    uint64_t extranonce_2,
    uint32_t current_version)
{
    if (!job) {
        return;
    }

    bm_job *next_job = malloc(sizeof(bm_job));

    if (next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for new job");
        return;
    }

    uint32_t version_mask = job->version_mask;
    double job_diff = job->pool_diff;

    uint8_t merkle_root[32];

    char extranonce_2_str[MAX_EXTRANONCE2_STR] = "";

    /*
     * Version handling remains the same as original code.
     */
    uint32_t effective_version = job->version;

    if (!GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling &&
        !miner_job_is_rollable(job)) {
        effective_version = current_version;
    }


    /*
     * ---------------------------------------------------------
     * SV2 STANDARD JOB
     * ---------------------------------------------------------
     */
    if (job->type == JOB_TYPE_SV2_STANDARD) {

        memcpy(merkle_root, job->merkle_root, 32);

    }
    else {

        /*
         * -----------------------------------------------------
         * EXTRANONCE2
         * -----------------------------------------------------
         *
         * IMPORTANT:
         *
         * extranonce_2 is intentionally ALWAYS ZERO.
         *
         * We do NOT increment it anywhere.
         *
         * The requested extranonce2 length from the pool is
         * preserved.
         *
         * Example:
         *
         * len = 4 bytes
         * -> 00 00 00 00
         *
         * hex:
         * -> 00000000
         *
         * len = 2 bytes
         * -> 0000
         */
        size_t e2_len = job->extranonce2_len;

        if (e2_len > MAX_EXTRANONCE2_LEN) {

            ESP_LOGE(
                TAG,
                "extranonce_2_len %u exceeds maximum %d, skipping job",
                (unsigned)e2_len,
                MAX_EXTRANONCE2_LEN
            );

            free(next_job);
            return;
        }


        /*
         * Completely zero extranonce2 buffer.
         *
         * Do NOT copy extranonce_2 into this buffer.
         */
        uint8_t extranonce_2_bin[MAX_EXTRANONCE2_LEN] = {0};


        /*
         * extranonce2 string is therefore always:
         *
         * e2_len = 4 -> 00000000
         * e2_len = 8 -> 0000000000000000
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
         * -----------------------------------------------------
         * EXTRANONCE1 ROLLING
         * -----------------------------------------------------
         *
         * Pool extranonce1:
         *
         * 20 04 53 83
         *
         * becomes:
         *
         * 00 04 53 83
         *
         *
         * Pool extranonce1:
         *
         * 30 49 87 65
         *
         * becomes:
         *
         * 10 49 87 65
         */
        uint8_t rolled_extranonce1[MAX_EXTRANONCE2_LEN] = {0};

        if (job->extranonce1_len > MAX_EXTRANONCE2_LEN) {

            ESP_LOGE(
                TAG,
                "extranonce1_len %u exceeds maximum %d, skipping job",
                (unsigned)job->extranonce1_len,
                MAX_EXTRANONCE2_LEN
            );

            free(next_job);
            return;
        }


        roll_extranonce1(
            job->extranonce1,
            rolled_extranonce1,
            job->extranonce1_len
        );


        /*
         * Optional debug output.
         *
         * This lets you verify exactly what is being hashed.
         */
        char original_extranonce1_str[MAX_EXTRANONCE2_STR] = "";
        char rolled_extranonce1_str[MAX_EXTRANONCE2_STR] = "";

        extranonce1_to_hex(
            job->extranonce1,
            job->extranonce1_len,
            original_extranonce1_str,
            sizeof(original_extranonce1_str)
        );

        extranonce1_to_hex(
            rolled_extranonce1,
            job->extranonce1_len,
            rolled_extranonce1_str,
            sizeof(rolled_extranonce1_str)
        );

        ESP_LOGI(
            TAG,
            "Extranonce1: %s -> %s | Extranonce2: %s",
            original_extranonce1_str,
            rolled_extranonce1_str,
            extranonce_2_str
        );


        /*
         * -----------------------------------------------------
         * COINBASE HASH
         * -----------------------------------------------------
         *
         * IMPORTANT:
         *
         * We use ROLLED extranonce1 here.
         *
         * Coinbase:
         *
         * prefix
         * +
         * rolled extranonce1
         * +
         * zero extranonce2
         * +
         * suffix
         */
        uint8_t coinbase_tx_hash[32];

        calculate_coinbase_tx_hash_bin(
            job->coinbase_prefix,
            job->coinbase_prefix_len,

            /*
             * ROLLED EXTRANONCE1
             */
            rolled_extranonce1,
            job->extranonce1_len,

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
         * Calculate merkle root from the new coinbase hash.
         */
        calculate_merkle_root_hash(
            coinbase_tx_hash,
            (const uint8_t (*)[32])job->merkle_path,
            job->merkle_path_count,
            merkle_root
        );
    }


    /*
     * ---------------------------------------------------------
     * BUILD BM JOB
     * ---------------------------------------------------------
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
    next_job->jobid = strdup(job->job_id);


    /*
     * IMPORTANT:
     *
     * next_job->extranonce2 is ALWAYS the zero value.
     *
     * Examples:
     *
     * 4 bytes -> 00000000
     * 8 bytes -> 0000000000000000
     */
    next_job->extranonce2 = strdup(extranonce_2_str);


    if (next_job->jobid == NULL ||
        next_job->extranonce2 == NULL) {

        ESP_LOGE(TAG, "Failed to allocate job metadata");

        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);

        return;
    }


    /*
     * ASIC initialization check.
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
     * Send work to ASIC.
     */
    ASIC_send_work(
        GLOBAL_STATE,
        next_job
    );
}


void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE =
        (GlobalState *)pvParameters;


    /*
     * active_jobs / valid_jobs are allocated and zeroed by
     * SYSTEM_init_system(), before any task that touches them
     * can run.
     */

    uint32_t current_version_mask = 0;

    miner_job_t *current_work = NULL;

    bool current_work_sent = false;


    /*
     * ---------------------------------------------------------
     * EXTRANONCE2 IS ALWAYS ZERO
     * ---------------------------------------------------------
     *
     * This variable is kept because generate_work_from_miner_job()
     * accepts it, but it is NEVER incremented.
     */
    uint64_t extranonce_2 = 0;


    uint32_t current_version = 0;

    int timeout_ms =
        ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);


    ESP_LOGI(
        TAG,
        "ASIC Job Interval: %d ms",
        timeout_ms
    );

    ESP_LOGI(
        TAG,
        "ASIC Ready!"
    );


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
            (esp_timer_get_time() - start_time) / 1000;


        /*
         * -----------------------------------------------------
         * NEW JOB
         * -----------------------------------------------------
         */
        if (notified == pdTRUE) {

            miner_job_t *new_work =
                miner_job_get_slot(
                    (size_t)slot_notify
                );


            ESP_LOGI(
                TAG,
                "New Work Activated (slot %lu) %s (type %d)",
                (unsigned long)slot_notify,
                new_work->job_id,
                new_work->type
            );


            current_work = new_work;


            GLOBAL_STATE->active_job_slot_idx =
                (uint8_t)(
                    slot_notify %
                    MINER_JOB_POOL_SIZE
                );


            current_work_sent = false;


            current_version =
                new_work->version;


            /*
             * Version mask handling remains unchanged.
             */
            if (new_work->version_mask != current_version_mask &&
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
             * -------------------------------------------------
             * RESET EXTRANONCE2
             * -------------------------------------------------
             *
             * New job = zero again.
             *
             * It will remain zero forever.
             */
            extranonce_2 = 0;


            if (!current_work->clean_jobs) {

                /*
                 * Staged job for next cycle,
                 * let current ASIC cycle finish.
                 */
                continue;
            }
        }
        else {

            /*
             * No new job.
             */
            if (current_work == NULL) {

                vTaskDelay(
                    100 / portTICK_PERIOD_MS
                );

                continue;
            }


            /*
             * Hardware version rolling handling.
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
         * -----------------------------------------------------
         * GENERATE WORK
         * -----------------------------------------------------
         *
         * extranonce_2 is ALWAYS ZERO.
         *
         * Inside generate_work_from_miner_job():
         *
         *   extranonce1 gets rolled
         *   extranonce2 remains zero
         *   coinbase is hashed
         *   merkle root is calculated
         *   work goes to ASIC
         */
        generate_work_from_miner_job(
            GLOBAL_STATE,
            current_work,
            0,                  /* ALWAYS ZERO */
            current_version
        );


        /*
         * Decode/apply coinbase only once per job,
         * same as original logic.
         */
        if (!current_work_sent) {

            SYSTEM_decode_and_apply_coinbase(
                GLOBAL_STATE,
                current_work
            );
        }


        current_work_sent = true;


        /*
         * -----------------------------------------------------
         * NO EXTRANONCE2 INCREMENT
         * -----------------------------------------------------
         *
         * ORIGINAL:
         *
         * if (miner_job_is_rollable(current_work)) {
         *     extranonce_2++;
         * }
         *
         * THAT IS INTENTIONALLY REMOVED.
         *
         * extranonce2 MUST stay zero.
         */


        /*
         * -----------------------------------------------------
         * SOFTWARE VERSION ROLLING
         * -----------------------------------------------------
         *
         * This part is retained from the original code.
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
         * Reset ASIC job interval.
         */
        timeout_ms =
            ASIC_get_asic_job_frequency_ms(
                GLOBAL_STATE
            );
    }
}
```

### Önemli nokta

Bu sürümde örneğin pool:

```text
extranonce1 = 20045383
extranonce2_len = 4
```

verirse ASIC tarafına hesaplanan coinbase:

```text
extranonce1 = 00045383
extranonce2 = 00000000
```

üzerinden oluşturuluyor.

`30498765` gelirse:

```text
extranonce1 = 10498765
extranonce2 = 00000000
```

oluyor.

**Ama bir noktaya dikkat:** Bu kod `extranonce1` değerini her iş üretiminde tekrar aynı şekilde `2→0 / 3→1` dönüştürüyor. Yani `20045383 → 00045383` sonrası `0045383` gibi ikinci bir rolling state üretmiyor. Senin tarif ettiğin kurala göre bu doğru davranış.

Bir de mevcut `SYSTEM_decode_and_apply_coinbase()` fonksiyonunun ne yaptığını görmek önemli; eğer o fonksiyon pool'dan gelen orijinal `extranonce1` ile ayrıca coinbase oluşturuyorsa, yukarıdaki değişiklik tek başına yeterli olmayabilir. O fonksiyonun kodunu da gönderirsen, **extranonce1 → rolling → extranonce2=0 → coinbase → merkle → ASIC** zincirinin tamamını aynı mantıkta düzeltebilirim.
