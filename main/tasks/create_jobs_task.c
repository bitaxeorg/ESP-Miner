```c
#include <sys/time.h>
#include <limits.h>
#include <inttypes.h>

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

#define MAX_EXTRANONCE2_LEN 32
#define MAX_EXTRANONCE2_STR (MAX_EXTRANONCE2_LEN * 2 + 1)

static void generate_work(GlobalState *GLOBAL_STATE,
                          mining_notify *notification,
                          double difficulty,
                          const char *rolling_extranonce1);

static void generate_work_sv2(GlobalState *GLOBAL_STATE,
                              sv2_job_t *job,
                              double difficulty);

static void generate_work_sv2_ext(GlobalState *GLOBAL_STATE,
                                  sv2_ext_job_t *job,
                                  double difficulty);


/*
 * ============================================================
 * EXTRANONCE1 ROLLING
 * ============================================================
 *
 * Pool:
 *
 *   20045383 -> 00045383
 *   30498765 -> 10498765
 *
 * After that:
 *
 *   00045383
 *   00045384
 *   00045385
 *   ...
 *
 * First hexadecimal character stays 0 or 1.
 */
static void roll_extranonce1_start(const char *pool_ex1,
                                   char *rolled_ex1,
                                   size_t size)
{
    if (!pool_ex1 || !rolled_ex1 || size < 2) {
        return;
    }

    strncpy(rolled_ex1, pool_ex1, size - 1);
    rolled_ex1[size - 1] = '\0';

    if (rolled_ex1[0] == '2') {
        rolled_ex1[0] = '0';
    } else if (rolled_ex1[0] == '3') {
        rolled_ex1[0] = '1';
    }
}


/*
 * Increment only the part after the first hexadecimal digit.
 *
 * 00045383 -> 00045384
 * 00045384 -> 00045385
 *
 * 10498765 -> 10498766
 *
 * 0fffffff -> 00000000
 * 1fffffff -> 10000000
 */
static void roll_extranonce1_next(char *ex1)
{
    if (!ex1 || ex1[0] == '\0') {
        return;
    }

    size_t len = strlen(ex1);

    if (len < 2) {
        return;
    }

    /*
     * First character is the rolling prefix.
     * Never modify it.
     */
    for (int i = (int)len - 1; i >= 1; i--) {

        char c = ex1[i];

        if (c >= '0' && c <= '8') {
            ex1[i] = c + 1;
            return;
        }

        if (c == '9') {
            ex1[i] = 'a';
            return;
        }

        if (c >= 'a' && c <= 'e') {
            ex1[i] = c + 1;
            return;
        }

        if (c == 'f') {
            ex1[i] = '0';
            continue;
        }

        if (c >= 'A' && c <= 'E') {
            ex1[i] = c + 1;
            return;
        }

        if (c == 'F') {
            ex1[i] = '0';
            continue;
        }
    }
}


// Free a work item using the correct free function for the protocol it was created under
static void free_work_item(GlobalState *GLOBAL_STATE,
                           void *work,
                           stratum_protocol_t protocol)
{
    if (!work) return;

    if (protocol == STRATUM_PROTOCOL_V2) {

        if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
            sv2_ext_job_free((sv2_ext_job_t *)work);
        } else {
            free(work);
        }

    } else {

        STRATUM_V1_free_mining_notify(work);
    }
}


void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE =
        (GlobalState *)pvParameters;

    double difficulty =
        GLOBAL_STATE->pool_difficulty;

    void *current_work = NULL;

    stratum_protocol_t current_work_protocol =
        GLOBAL_STATE->stratum_protocol;

    int timeout_ms =
        ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);


    /*
     * ========================================================
     * V1 EXTRANONCE1 ROLLING STATE
     * ========================================================
     */
    static char rolling_extranonce1[
        MAX_EXTRANONCE2_STR
    ] = {0};

    static bool rolling_extranonce1_valid = false;


    // En son ASIC'e gönderilen işin ID'sini takip eden hafıza
    static char last_dispatched_job_v1[64] = {0};
    static uint32_t last_dispatched_job_sv2 = UINT32_MAX;


    ESP_LOGI(
        TAG,
        "ASIC Job Interval: %d ms",
        timeout_ms
    );

    ESP_LOGI(
        TAG,
        "ASIC Ready! (Zero-Extranonce2 + Extranonce1-Rolling + BIP320)"
    );


    while (1) {

        if (GLOBAL_STATE->reset_extranonce2) {
            GLOBAL_STATE->reset_extranonce2 = false;
        }


        /*
         * ====================================================
         * PROTOCOL CHANGE
         * ====================================================
         */
        stratum_protocol_t active_protocol =
            GLOBAL_STATE->stratum_protocol;


        if (active_protocol != current_work_protocol) {

            if (current_work != NULL) {

                ESP_LOGI(
                    TAG,
                    "Protocol switched from %s to %s, discarding current work",
                    current_work_protocol == STRATUM_PROTOCOL_V2
                        ? STRATUM_V2
                        : STRATUM_V1,
                    active_protocol == STRATUM_PROTOCOL_V2
                        ? STRATUM_V2
                        : STRATUM_V1
                );

                free_work_item(
                    GLOBAL_STATE,
                    current_work,
                    current_work_protocol
                );

                current_work = NULL;
            }


            current_work_protocol =
                active_protocol;


            last_dispatched_job_v1[0] =
                '\0';

            last_dispatched_job_sv2 =
                UINT32_MAX;


            rolling_extranonce1[0] =
                '\0';

            rolling_extranonce1_valid =
                false;
        }


        /*
         * ====================================================
         * WAIT FOR NEW WORK
         * ====================================================
         */
        uint64_t start_time =
            esp_timer_get_time();


        void *new_work =
            queue_dequeue_timeout(
                &GLOBAL_STATE->stratum_queue,
                timeout_ms
            );


        timeout_ms -=
            (esp_timer_get_time() - start_time) / 1000;


        /*
         * ====================================================
         * NEW WORK ARRIVED
         * ====================================================
         */
        if (new_work != NULL) {

            active_protocol =
                GLOBAL_STATE->stratum_protocol;


            /*
             * Free previous job.
             */
            free_work_item(
                GLOBAL_STATE,
                current_work,
                current_work_protocol
            );

            current_work = NULL;


            if (active_protocol != current_work_protocol) {

                ESP_LOGW(
                    TAG,
                    "Protocol switch detected during dequeue, discarding stale item"
                );

                free(new_work);

                current_work_protocol =
                    active_protocol;

                rolling_extranonce1[0] =
                    '\0';

                rolling_extranonce1_valid =
                    false;

                timeout_ms =
                    ASIC_get_asic_job_frequency_ms(
                        GLOBAL_STATE
                    );

                continue;
            }


            current_work =
                new_work;


            /*
             * =================================================
             * JOB INFORMATION
             * =================================================
             */
            bool is_new_job_id = false;
            bool clean = false;


            if (current_work_protocol ==
                STRATUM_PROTOCOL_V2) {

                if (stratum_v2_is_extended_channel(
                        GLOBAL_STATE)) {

                    sv2_ext_job_t *j =
                        (sv2_ext_job_t *)current_work;


                    ESP_LOGI(
                        TAG,
                        "New Work Dequeued SV2 ext job %lu",
                        j->job_id
                    );


                    clean =
                        j->clean_jobs;


                    if (last_dispatched_job_sv2 !=
                        j->job_id) {

                        is_new_job_id =
                            true;

                        last_dispatched_job_sv2 =
                            j->job_id;
                    }

                } else {

                    sv2_job_t *j =
                        (sv2_job_t *)current_work;


                    ESP_LOGI(
                        TAG,
                        "New Work Dequeued SV2 job %lu",
                        j->job_id
                    );


                    clean =
                        j->clean_jobs;


                    if (last_dispatched_job_sv2 !=
                        j->job_id) {

                        is_new_job_id =
                            true;

                        last_dispatched_job_sv2 =
                            j->job_id;
                    }
                }

            } else {

                /*
                 * =================================================
                 * V1 NEW JOB
                 * =================================================
                 */
                mining_notify *j =
                    (mining_notify *)current_work;


                ESP_LOGI(
                    TAG,
                    "New Work Dequeued %s (clean: %s)",
                    j->job_id,
                    j->clean_jobs
                        ? "true"
                        : "false"
                );


                clean =
                    j->clean_jobs;


                if (strcmp(
                        last_dispatched_job_v1,
                        j->job_id) != 0) {

                    is_new_job_id =
                        true;


                    strncpy(
                        last_dispatched_job_v1,
                        j->job_id,
                        sizeof(last_dispatched_job_v1) - 1
                    );

                    last_dispatched_job_v1[
                        sizeof(last_dispatched_job_v1) - 1
                    ] = '\0';
                }


                /*
                 * =================================================
                 * START EXTRANONCE1 ROLLING
                 * =================================================
                 *
                 * Pool:
                 *
                 * 20045383
                 *
                 * becomes:
                 *
                 * 00045383
                 *
                 * Pool:
                 *
                 * 30498765
                 *
                 * becomes:
                 *
                 * 10498765
                 */
                roll_extranonce1_start(
                    GLOBAL_STATE->extranonce_str,
                    rolling_extranonce1,
                    sizeof(rolling_extranonce1)
                );


                rolling_extranonce1_valid =
                    (rolling_extranonce1[0] != '\0');


                ESP_LOGI(
                    TAG,
                    "EXTRANONCE1 ROLL START: %s -> %s",
                    GLOBAL_STATE->extranonce_str,
                    rolling_extranonce1
                );
            }


            /*
             * =================================================
             * DIFFICULTY
             * =================================================
             */
            if (GLOBAL_STATE->new_set_mining_difficulty_msg) {

                ESP_LOGI(
                    TAG,
                    "New pool difficulty %.2f",
                    GLOBAL_STATE->pool_difficulty
                );


                difficulty =
                    GLOBAL_STATE->pool_difficulty;


                GLOBAL_STATE->new_set_mining_difficulty_msg =
                    false;
            }


            /*
             * =================================================
             * VERSION ROLLING
             * =================================================
             */
            if (GLOBAL_STATE->new_stratum_version_rolling_msg &&
                GLOBAL_STATE->ASIC_initalized) {

                ESP_LOGI(
                    TAG,
                    "Set chip version rolls %i",
                    (int)(
                        GLOBAL_STATE->version_mask >> 13
                    )
                );


                ASIC_set_version_mask(
                    GLOBAL_STATE,
                    GLOBAL_STATE->version_mask
                );


                GLOBAL_STATE->new_stratum_version_rolling_msg =
                    false;
            }


            /*
             * =================================================
             * NEW JOB ID ALWAYS GETS SENT
             * =================================================
             */
            if (!is_new_job_id && !clean) {
                continue;
            }

        } else {

            /*
             * =================================================
             * QUEUE TIMEOUT
             * =================================================
             *
             * No new pool job.
             *
             * For V1:
             *
             * keep the SAME pool job,
             * change extranonce1,
             * recalculate coinbase,
             * recalculate merkle,
             * send new work.
             */
            if (current_work == NULL) {

                vTaskDelay(
                    100 / portTICK_PERIOD_MS
                );

                continue;
            }


            /*
             * =================================================
             * V1 LOCAL EXTRANONCE1 ROLLING
             * =================================================
             */
            if (current_work_protocol ==
                    STRATUM_PROTOCOL_V1 &&
                rolling_extranonce1_valid) {

                generate_work(
                    GLOBAL_STATE,
                    (mining_notify *)current_work,
                    difficulty,
                    rolling_extranonce1
                );


                /*
                 * Move to next extranonce1 AFTER
                 * current work has been dispatched.
                 */
                roll_extranonce1_next(
                    rolling_extranonce1
                );


                ESP_LOGD(
                    TAG,
                    "EXTRANONCE1 NEXT: %s",
                    rolling_extranonce1
                );
            }


            timeout_ms =
                ASIC_get_asic_job_frequency_ms(
                    GLOBAL_STATE
                );

            continue;
        }


        /*
         * ====================================================
         * FINAL PROTOCOL CHECK
         * ====================================================
         */
        active_protocol =
            GLOBAL_STATE->stratum_protocol;


        if (active_protocol !=
            current_work_protocol) {

            free_work_item(
                GLOBAL_STATE,
                current_work,
                current_work_protocol
            );


            current_work = NULL;

            current_work_protocol =
                active_protocol;


            rolling_extranonce1[0] =
                '\0';

            rolling_extranonce1_valid =
                false;


            timeout_ms =
                ASIC_get_asic_job_frequency_ms(
                    GLOBAL_STATE
                );

            continue;
        }


        /*
         * ====================================================
         * DISPATCH FRESH WORK
         * ====================================================
         */
        if (active_protocol ==
            STRATUM_PROTOCOL_V2) {

            if (stratum_v2_is_extended_channel(
                    GLOBAL_STATE)) {

                generate_work_sv2_ext(
                    GLOBAL_STATE,
                    (sv2_ext_job_t *)current_work,
                    difficulty
                );

            } else {

                generate_work_sv2(
                    GLOBAL_STATE,
                    (sv2_job_t *)current_work,
                    difficulty
                );
            }

        } else {

            /*
             * =================================================
             * V1
             * =================================================
             *
             * Send current rolling extranonce1.
             */
            generate_work(
                GLOBAL_STATE,
                (mining_notify *)current_work,
                difficulty,
                rolling_extranonce1
            );


            /*
             * Prepare next value.
             */
            if (rolling_extranonce1_valid) {

                roll_extranonce1_next(
                    rolling_extranonce1
                );


                ESP_LOGD(
                    TAG,
                    "EXTRANONCE1 NEXT: %s",
                    rolling_extranonce1
                );
            }
        }


        timeout_ms =
            ASIC_get_asic_job_frequency_ms(
                GLOBAL_STATE
            );
    }
}


/*
 * ============================================================
 * V1 WORK
 * ============================================================
 */
static void generate_work(
    GlobalState *GLOBAL_STATE,
    mining_notify *notification,
    double difficulty,
    const char *rolling_extranonce1)
{
    if (GLOBAL_STATE->extranonce_2_len >
        MAX_EXTRANONCE2_LEN) {

        ESP_LOGE(
            TAG,
            "extranonce_2_len %d exceeds maximum %d, skipping job",
            GLOBAL_STATE->extranonce_2_len,
            MAX_EXTRANONCE2_LEN
        );

        return;
    }


    /*
     * ========================================================
     * EXTRANONCE2 ALWAYS ZERO
     * ========================================================
     */
    char extranonce_2_str[
        MAX_EXTRANONCE2_STR
    ];


    memset(
        extranonce_2_str,
        '0',
        GLOBAL_STATE->extranonce_2_len * 2
    );


    extranonce_2_str[
        GLOBAL_STATE->extranonce_2_len * 2
    ] = '\0';


    /*
     * ========================================================
     * COINBASE HASH
     * ========================================================
     *
     * Use ROLLED extranonce1 instead of the pool's original
     * extranonce1.
     */
    uint8_t coinbase_tx_hash[32];


    calculate_coinbase_tx_hash(
        notification->coinbase_1,
        notification->coinbase_2,

        /*
         * ROLLED EXTRANONCE1
         */
        rolling_extranonce1,

        /*
         * ALWAYS ZERO EXTRANONCE2
         */
        extranonce_2_str,

        coinbase_tx_hash
    );


    /*
     * ========================================================
     * MERKLE ROOT
     * ========================================================
     */
    uint8_t merkle_root[32];


    calculate_merkle_root_hash(
        coinbase_tx_hash,
        (uint8_t(*)[32])
            notification->merkle_branches,
        notification->n_merkle_branches,
        merkle_root
    );


    /*
     * ========================================================
     * BM JOB
     * ========================================================
     */
    bm_job *next_job =
        malloc(sizeof(bm_job));


    if (next_job == NULL) {

        ESP_LOGE(
            TAG,
            "Failed to allocate memory for new job"
        );

        return;
    }


    construct_bm_job(
        notification,
        merkle_root,
        GLOBAL_STATE->version_mask,
        difficulty,
        next_job
    );


    /*
     * ASIC metadata.
     */
    next_job->extranonce2 =
        strdup(extranonce_2_str);


    next_job->jobid =
        strdup(notification->job_id);


    next_job->version_mask =
        GLOBAL_STATE->version_mask;


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
 * SV2 STANDARD
 * ============================================================
 */
static void generate_work_sv2(
    GlobalState *GLOBAL_STATE,
    sv2_job_t *sv2_job,
    double difficulty)
{
    bm_job *next_job =
        malloc(sizeof(bm_job));


    if (next_job == NULL) {

        ESP_LOGE(
            TAG,
            "Failed to allocate memory for new SV2 job"
        );

        return;
    }


    uint32_t version_mask =
        GLOBAL_STATE->version_mask;


    next_job->version =
        sv2_job->version;

    next_job->target =
        sv2_job->nbits;

    next_job->ntime =
        sv2_job->ntime;

    next_job->starting_nonce =
        0;

    next_job->pool_diff =
        difficulty;


    reverse_32bit_words(
        sv2_job->merkle_root,
        next_job->merkle_root
    );


    reverse_32bit_words(
        sv2_job->prev_hash,
        next_job->prev_block_hash
    );


    uint8_t midstate_data[64];


    uint32_t base_version =
        sv2_job->version;


    memcpy(
        midstate_data,
        &base_version,
        4
    );


    memcpy(
        midstate_data + 4,
        sv2_job->prev_hash,
        32
    );


    memcpy(
        midstate_data + 36,
        sv2_job->merkle_root,
        28
    );


    uint8_t midstate[32];


    midstate_sha256_bin(
        midstate_data,
        64,
        midstate
    );


    reverse_32bit_words(
        midstate,
        next_job->midstate
    );


    if (version_mask != 0) {

        uint32_t rolled_version =
            increment_bitmask(
                base_version,
                version_mask
            );


        memcpy(
            midstate_data,
            &rolled_version,
            4
        );


        midstate_sha256_bin(
            midstate_data,
            64,
            midstate
        );


        reverse_32bit_words(
            midstate,
            next_job->midstate1
        );


        rolled_version =
            increment_bitmask(
                rolled_version,
                version_mask
            );


        memcpy(
            midstate_data,
            &rolled_version,
            4
        );


        midstate_sha256_bin(
            midstate_data,
            64,
            midstate
        );


        reverse_32bit_words(
            midstate,
            next_job->midstate2
        );


        rolled_version =
            increment_bitmask(
                rolled_version,
                version_mask
            );


        memcpy(
            midstate_data,
            &rolled_version,
            4
        );


        midstate_sha256_bin(
            midstate_data,
            64,
            midstate
        );


        reverse_32bit_words(
            midstate,
            next_job->midstate3
        );


        next_job->num_midstates =
            4;

    } else {

        next_job->num_midstates =
            1;
    }


    char jobid_str[16];


    snprintf(
        jobid_str,
        sizeof(jobid_str),
        "%" PRIu32,
        sv2_job->job_id
    );


    next_job->jobid =
        strdup(jobid_str);


    next_job->extranonce2 =
        strdup("");


    next_job->version_mask =
        version_mask;


    if (!GLOBAL_STATE->ASIC_initalized) {

        ESP_LOGW(
            TAG,
            "ASIC not initialized, skipping SV2 job send"
        );

        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);

        return;
    }


    ASIC_send_work(
        GLOBAL_STATE,
        next_job
    );
}


/*
 * ============================================================
 * SV2 EXTENDED
 * ============================================================
 */
static void generate_work_sv2_ext(
    GlobalState *GLOBAL_STATE,
    sv2_ext_job_t *ext_job,
    double difficulty)
{
    sv2_conn_t *conn =
        GLOBAL_STATE->sv2_conn;


    if (!conn) {
        return;
    }


    bm_job *next_job =
        malloc(sizeof(bm_job));


    if (!next_job) {

        ESP_LOGE(
            TAG,
            "Failed to allocate memory for SV2 ext job"
        );

        return;
    }


    uint32_t version_mask =
        GLOBAL_STATE->version_mask;


    uint8_t extranonce_2_len =
        conn->extranonce_size;


    uint8_t extranonce_2[32];


    /*
     * SV2 extranonce2 remains zero.
     */
    memset(
        extranonce_2,
        0,
        sizeof(extranonce_2)
    );


    uint8_t coinbase_tx_hash[32];


    calculate_coinbase_tx_hash_bin(
        ext_job->coinbase_prefix,
        ext_job->coinbase_prefix_len,

        conn->extranonce_prefix,
        conn->extranonce_prefix_len,

        extranonce_2,
        extranonce_2_len,

        ext_job->coinbase_suffix,
        ext_job->coinbase_suffix_len,

        coinbase_tx_hash
    );


    uint8_t merkle_root[32];


    calculate_merkle_root_hash(
        coinbase_tx_hash,
        (const uint8_t (*)[32])
            ext_job->merkle_path,
        ext_job->merkle_path_count,
        merkle_root
    );


    next_job->version =
        ext_job->version;

    next_job->target =
        ext_job->nbits;

    next_job->ntime =
        ext_job->ntime;

    next_job->starting_nonce =
        0;

    next_job->pool_diff =
        difficulty;


    reverse_32bit_words(
        merkle_root,
        next_job->merkle_root
    );


    reverse_32bit_words(
        ext_job->prev_hash,
        next_job->prev_block_hash
    );


    uint8_t midstate_data[64];


    uint32_t base_version =
        ext_job->version;


    memcpy(
        midstate_data,
        &base_version,
        4
    );


    memcpy(
        midstate_data + 4,
        ext_job->prev_hash,
        32
    );


    memcpy(
        midstate_data + 36,
        merkle_root,
        28
    );


    uint8_t midstate[32];


    midstate_sha256_bin(
        midstate_data,
        64,
        midstate
    );


    reverse_32bit_words(
        midstate,
        next_job->midstate
    );


    if (version_mask != 0) {

        uint32_t rolled_version =
            increment_bitmask(
                base_version,
                version_mask
            );


        memcpy(
            midstate_data,
            &rolled_version,
            4
        );


        midstate_sha256_bin(
            midstate_data,
            64,
            midstate
        );


        reverse_32bit_words(
            midstate,
            next_job->midstate1
        );


        rolled_version =
            increment_bitmask(
                rolled_version,
                version_mask
            );


        memcpy(
            midstate_data,
            &rolled_version,
            4
        );


        midstate_sha256_bin(
            midstate_data,
            64,
            midstate
        );


        reverse_32bit_words(
            midstate,
            next_job->midstate2
        );


        rolled_version =
            increment_bitmask(
                rolled_version,
                version_mask
            );


        memcpy(
            midstate_data,
            &rolled_version,
            4
        );


        midstate_sha256_bin(
            midstate_data,
            64,
            midstate
        );


        reverse_32bit_words(
            midstate,
            next_job->midstate3
        );


        next_job->num_midstates =
            4;

    } else {

        next_job->num_midstates =
            1;
    }


    char jobid_str[16];


    snprintf(
        jobid_str,
        sizeof(jobid_str),
        "%" PRIu32,
        ext_job->job_id
    );


    next_job->jobid =
        strdup(jobid_str);


    char en2_hex[65];


    bin2hex(
        extranonce_2,
        extranonce_2_len,
        en2_hex,
        sizeof(en2_hex)
    );


    next_job->extranonce2 =
        strdup(en2_hex);


    next_job->version_mask =
        version_mask;


    if (!GLOBAL_STATE->ASIC_initalized) {

        ESP_LOGW(
            TAG,
            "ASIC not initialized, skipping SV2 ext job send"
        );

        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);

        return;
    }


    ASIC_send_work(
        GLOBAL_STATE,
        next_job
    );
}
```
