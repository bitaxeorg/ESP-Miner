#include "esp_log.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "system.h"
#include "global_state.h"
#include "sv2_task.h"
#include "sv2_api.h"
#include "sv2_noise.h"
#include "stratum_task.h"
#include "nvs_config.h"
#include "work_queue.h"
#include "utils.h"
#include "libbase58.h"

#include <lwip/netdb.h>
#include <string.h>
#include <stdlib.h>

#define MAX_RETRY_ATTEMPTS 3
#define TRANSPORT_TIMEOUT_MS 5000
#define SV2_MAX_FRAME_SIZE 512

static const char *TAG = "sv2_task";

static bool is_wifi_connected(void)
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

static void sv2_set_socket_options(esp_transport_handle_t transport)
{
    int sock = esp_transport_get_socket(transport);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to get socket from transport");
        return;
    }

    struct timeval snd_timeout = { .tv_sec = 5, .tv_usec = 0 };
    struct timeval rcv_timeout = { .tv_sec = 60 * 3, .tv_usec = 0 };

    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout, sizeof(snd_timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));

    int keepalive = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

    int keepidle = 60;
    int keepintvl = 10;
    int keepcnt = 3;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
}

// Load authority pubkey from NVS (base58-encoded) into 32-byte buffer.
// SV2 format: base58check(0x0001_LE + 32_byte_xonly_pubkey)
// Decoded: 2-byte version + 32-byte pubkey + 4-byte checksum = 38 bytes
// Returns true if a valid base58 pubkey was decoded.
static bool sv2_load_authority_pubkey(uint8_t out[32])
{
    char *b58_key = nvs_config_get_string(NVS_CONFIG_SV2_AUTHORITY_PUBKEY);
    if (!b58_key || strlen(b58_key) == 0) {
        free(b58_key);
        return false;
    }

    // Decode raw base58 (includes 4-byte checksum)
    uint8_t decoded[64];
    size_t decoded_len = sizeof(decoded);

    if (!b58tobin(decoded, &decoded_len, b58_key, 0)) {
        ESP_LOGE(TAG, "Failed to decode base58 authority pubkey");
        free(b58_key);
        return false;
    }
    free(b58_key);

    // base58check = 2-byte version + 32-byte pubkey + 4-byte checksum = 38 bytes
    if (decoded_len != 38) {
        ESP_LOGE(TAG, "Invalid decoded length: %zu (expected 38)", decoded_len);
        return false;
    }

    // b58tobin right-aligns data in the buffer; compute actual data offset
    uint8_t *data = decoded + (sizeof(decoded) - decoded_len);

    // Verify version (0x0001 in little-endian: bytes[0]=0x01, bytes[1]=0x00)
    if (data[0] != 0x01 || data[1] != 0x00) {
        ESP_LOGE(TAG, "Invalid key version: 0x%02x%02x (expected 0x0100)", data[1], data[0]);
        return false;
    }

    // Extract the 32-byte x-only public key (skip 2-byte version, ignore 4-byte checksum)
    memcpy(out, data + 2, 32);

    ESP_LOGI(TAG, "Successfully decoded base58 authority pubkey");
    return true;
}

static void sv2_clean_queue(GlobalState *GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Clean Jobs: clearing queue");
    queue_clear(&GLOBAL_STATE->stratum_queue);

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    for (int i = 0; i < 128; i = i + 4) {
        GLOBAL_STATE->valid_jobs[i] = 0;
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
}

void sv2_close_connection(GlobalState *GLOBAL_STATE)
{
    ESP_LOGE(TAG, "Shutting down SV2 connection and restarting...");
    if (GLOBAL_STATE->sv2_noise_ctx) {
        sv2_noise_destroy(GLOBAL_STATE->sv2_noise_ctx);
        GLOBAL_STATE->sv2_noise_ctx = NULL;
    }
    if (GLOBAL_STATE->transport) {
        esp_transport_close(GLOBAL_STATE->transport);
        esp_transport_destroy(GLOBAL_STATE->transport);
        GLOBAL_STATE->transport = NULL;
    }
    sv2_clean_queue(GLOBAL_STATE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// Track last share submit time for response time measurement
static int64_t sv2_last_submit_time_us = 0;

int sv2_submit_share(GlobalState *GLOBAL_STATE, uint32_t job_id, uint32_t nonce,
                     uint32_t ntime, uint32_t version)
{
    if (!GLOBAL_STATE->transport || !GLOBAL_STATE->sv2_conn || !GLOBAL_STATE->sv2_noise_ctx) {
        return -1;
    }

    sv2_conn_t *conn = GLOBAL_STATE->sv2_conn;
    uint8_t buf[SV2_FRAME_HEADER_SIZE + 24]; // SubmitSharesStandard is exactly 24 bytes payload

    int len = sv2_build_submit_shares_standard(buf, sizeof(buf),
                                                conn->channel_id,
                                                conn->sequence_number++,
                                                job_id, nonce, ntime, version);
    if (len < 0) return -1;

    sv2_last_submit_time_us = esp_timer_get_time();
    return sv2_noise_send(GLOBAL_STATE->sv2_noise_ctx, GLOBAL_STATE->transport, buf, len);
}

// Enqueue an sv2_job_t onto the stratum queue
static void sv2_enqueue_job(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                            uint32_t job_id, uint32_t version,
                            const uint8_t merkle_root[32], const uint8_t prev_hash[32],
                            uint32_t ntime, uint32_t nbits, bool clean_jobs)
{
    sv2_job_t *job = malloc(sizeof(sv2_job_t));
    if (!job) {
        ESP_LOGE(TAG, "Failed to allocate sv2_job_t");
        return;
    }

    job->job_id = job_id;
    job->version = version;
    memcpy(job->merkle_root, merkle_root, 32);
    memcpy(job->prev_hash, prev_hash, 32);
    job->ntime = ntime;
    job->nbits = nbits;
    job->clean_jobs = clean_jobs;

    GLOBAL_STATE->SYSTEM_MODULE.work_received++;

    SYSTEM_notify_new_ntime(GLOBAL_STATE, ntime);

    if (clean_jobs && (GLOBAL_STATE->stratum_queue.count > 0)) {
        sv2_clean_queue(GLOBAL_STATE);
    }

    if (GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
        void *old = queue_dequeue(&GLOBAL_STATE->stratum_queue);
        free(old);
    }

    queue_enqueue(&GLOBAL_STATE->stratum_queue, job);
}

// Handle NewMiningJob message
static void sv2_handle_new_mining_job(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                      const uint8_t *payload, uint32_t len)
{
    uint32_t channel_id, job_id, version, min_ntime;
    bool has_min_ntime;
    uint8_t merkle_root[32];

    if (sv2_parse_new_mining_job(payload, len, &channel_id, &job_id,
                                 &has_min_ntime, &min_ntime,
                                 &version, merkle_root) != 0) {
        ESP_LOGE(TAG, "Failed to parse NewMiningJob");
        return;
    }

    // SV2 spec: min_ntime present = mine immediately; absent = wait for SetNewPrevHash
    ESP_LOGI(TAG, "New mining job: id=%lu, version=%08lx, future=%s",
             job_id, version, has_min_ntime ? "no" : "yes");

    int slot = job_id % SV2_PENDING_JOBS_SIZE;

    if (has_min_ntime) {
        // Job has min_ntime set: mine immediately with current prev_hash
        if (conn->has_prev_hash) {
            sv2_enqueue_job(GLOBAL_STATE, conn, job_id, version, merkle_root,
                            conn->prev_hash, min_ntime,
                            conn->prev_hash_nbits, true);
        } else {
            // No prev_hash yet, store in pending
            conn->pending_jobs[slot].job_id = job_id;
            conn->pending_jobs[slot].version = version;
            memcpy(conn->pending_jobs[slot].merkle_root, merkle_root, 32);
            conn->pending_jobs[slot].valid = true;
        }
    } else {
        // No min_ntime: wait for SetNewPrevHash to provide prev_hash and ntime
        conn->pending_jobs[slot].job_id = job_id;
        conn->pending_jobs[slot].version = version;
        memcpy(conn->pending_jobs[slot].merkle_root, merkle_root, 32);
        conn->pending_jobs[slot].valid = true;
    }
}

// Handle SetNewPrevHash message
static void sv2_handle_set_new_prev_hash(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                         const uint8_t *payload, uint32_t len)
{
    uint32_t channel_id, job_id, min_ntime, nbits;
    uint8_t prev_hash[32];

    if (sv2_parse_set_new_prev_hash(payload, len, &channel_id, &job_id,
                                     prev_hash, &min_ntime, &nbits) != 0) {
        ESP_LOGE(TAG, "Failed to parse SetNewPrevHash");
        return;
    }

    ESP_LOGI(TAG, "New prev_hash: job_id=%lu, ntime=%lu, nbits=%08lx", job_id, min_ntime, nbits);

    // Update network difficulty from nbits (same computation as V1)
    GLOBAL_STATE->network_nonce_diff = (uint64_t) networkDifficulty(nbits);
    suffixString(GLOBAL_STATE->network_nonce_diff, GLOBAL_STATE->network_diff_string, DIFF_STRING_SIZE, 0);

    bool first_prev_hash = !conn->has_prev_hash;

    // Store as latest prev_hash
    memcpy(conn->prev_hash, prev_hash, 32);
    conn->prev_hash_ntime = min_ntime;
    conn->prev_hash_nbits = nbits;
    conn->has_prev_hash = true;

    // Look for matching pending job
    int slot = job_id % SV2_PENDING_JOBS_SIZE;
    if (conn->pending_jobs[slot].valid && conn->pending_jobs[slot].job_id == job_id) {
        sv2_enqueue_job(GLOBAL_STATE, conn, job_id,
                        conn->pending_jobs[slot].version,
                        conn->pending_jobs[slot].merkle_root,
                        prev_hash, min_ntime, nbits, true);
        conn->pending_jobs[slot].valid = false;
    }

    // If this is the first prev_hash, enqueue any pending future jobs
    // (future jobs that arrived before the first SetNewPrevHash)
    if (first_prev_hash) {
        for (int i = 0; i < SV2_PENDING_JOBS_SIZE; i++) {
            if (conn->pending_jobs[i].valid && conn->pending_jobs[i].job_id != job_id) {
                ESP_LOGD(TAG, "Enqueuing pending future job %lu with first prev_hash",
                         conn->pending_jobs[i].job_id);
                sv2_enqueue_job(GLOBAL_STATE, conn, conn->pending_jobs[i].job_id,
                                conn->pending_jobs[i].version,
                                conn->pending_jobs[i].merkle_root,
                                prev_hash, min_ntime, nbits, true);
                conn->pending_jobs[i].valid = false;
            }
        }
    }
}

// Handle SetTarget message
static void sv2_handle_set_target(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                  const uint8_t *payload, uint32_t len)
{
    uint32_t channel_id;
    uint8_t max_target[32];

    if (sv2_parse_set_target(payload, len, &channel_id, max_target) != 0) {
        ESP_LOGE(TAG, "Failed to parse SetTarget");
        return;
    }

    memcpy(conn->target, max_target, 32);
    uint32_t pdiff = sv2_target_to_pdiff(max_target);
    ESP_LOGI(TAG, "Set pool difficulty: %lu", pdiff);
    GLOBAL_STATE->pool_difficulty = pdiff;
    GLOBAL_STATE->new_set_mining_difficulty_msg = true;
}

void sv2_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // Set V2-specific free function for the work queue (sv2_job_t is flat, just free())
    GLOBAL_STATE->stratum_queue.free_fn = free;

    // Set default version mask for version rolling
    GLOBAL_STATE->version_mask = STRATUM_DEFAULT_VERSION_MASK;
    GLOBAL_STATE->new_stratum_version_rolling_msg = true;

    sv2_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    GLOBAL_STATE->sv2_conn = &conn;

    int retry_attempts = 0;

    char *stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    uint16_t port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;

    ESP_LOGI(TAG, "Starting SV2 task, connecting to %s:%d (free heap: %lu)",
             stratum_url, port, (unsigned long)esp_get_free_heap_size());

    while (1) {
        if (!is_wifi_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, waiting...");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        if (retry_attempts >= MAX_RETRY_ATTEMPTS) {
            // Check if a V1 fallback pool is configured
            if (GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url != NULL &&
                GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url[0] != '\0') {
                ESP_LOGW(TAG, "Max SV2 retry attempts reached (%d), falling back to V1 stratum pool",
                         retry_attempts);

                // Close any lingering SV2 connection state
                sv2_close_connection(GLOBAL_STATE);

                // Switch protocol to V1 so other tasks (create_jobs, asic_result) adapt
                GLOBAL_STATE->stratum_protocol = STRATUM_V1;

                // Signal fallback mode
                GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = true;

                // Reset share stats (same pattern as V1 failover)
                for (int i = 0; i < GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats_count; i++) {
                    GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats[i].count = 0;
                    GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats[i].message[0] = '\0';
                }
                GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats_count = 0;
                GLOBAL_STATE->SYSTEM_MODULE.shares_accepted = 0;
                GLOBAL_STATE->SYSTEM_MODULE.shares_rejected = 0;
                GLOBAL_STATE->SYSTEM_MODULE.work_received = 0;

                // Start V1 stratum task (stack 8192, priority 5, same as main.c)
                if (xTaskCreate(stratum_task, "stratum admin", 8192, (void *) GLOBAL_STATE, 5, NULL) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to create V1 stratum task!");
                }

                // Delete this SV2 task
                GLOBAL_STATE->sv2_conn = NULL;
                vTaskDelete(NULL);
            }

            // No fallback configured — reset count and keep retrying SV2
            ESP_LOGE(TAG, "Max retry attempts reached (%d), no fallback configured, resetting count", retry_attempts);
            retry_attempts = 0;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Connecting to stratum+sv2://%s:%d (attempt %d)", stratum_url, port, retry_attempts + 1);

        // Create plain TCP transport
        esp_transport_handle_t transport = esp_transport_tcp_init();
        if (!transport) {
            ESP_LOGE(TAG, "Failed to init TCP transport");
            retry_attempts++;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        int64_t connect_start_us = esp_timer_get_time();

        esp_err_t ret = esp_transport_connect(transport, stratum_url, port, TRANSPORT_TIMEOUT_MS);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "TCP connect failed to %s:%d (err %d)", stratum_url, port, ret);
            esp_transport_close(transport);
            esp_transport_destroy(transport);
            retry_attempts++;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "TCP connected to %s:%d", stratum_url, port);

        GLOBAL_STATE->transport = transport;
        sv2_set_socket_options(transport);

        // Reset connection state
        memset(&conn, 0, sizeof(conn));

        // --- Noise Handshake ---
        ESP_LOGI(TAG, "Starting Noise handshake (Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256)");

        sv2_noise_ctx_t *noise_ctx = sv2_noise_create();
        if (!noise_ctx) {
            ESP_LOGE(TAG, "Failed to create noise context");
            sv2_close_connection(GLOBAL_STATE);
            retry_attempts++;
            continue;
        }
        GLOBAL_STATE->sv2_noise_ctx = noise_ctx;

        // Load optional authority pubkey from NVS
        uint8_t auth_key[32];
        bool has_auth = sv2_load_authority_pubkey(auth_key);
        if (has_auth) {
            ESP_LOGI(TAG, "Authority pubkey configured, will verify server certificate");
        } else {
            ESP_LOGW(TAG, "No authority pubkey configured (TOFU mode)");
        }

        if (sv2_noise_handshake(noise_ctx, transport, has_auth ? auth_key : NULL) != 0) {
            ESP_LOGE(TAG, "Noise handshake failed, reconnecting...");
            sv2_close_connection(GLOBAL_STATE);
            retry_attempts++;
            continue;
        }

        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2+Noise");

        ESP_LOGI(TAG, "Encrypted channel established (ChaCha20-Poly1305) (free heap: %lu)",
                 (unsigned long)esp_get_free_heap_size());

        // --- SV2 Protocol Handshake (encrypted) ---

        uint8_t frame_buf[SV2_MAX_FRAME_SIZE];
        uint8_t recv_buf[SV2_MAX_FRAME_SIZE];
        uint8_t hdr_buf[6];
        sv2_frame_header_t hdr;
        int payload_len;

        // 1. Send SetupConnection
        {
            const char *device_model = GLOBAL_STATE->DEVICE_CONFIG.family.asic.name;
            ESP_LOGI(TAG, "Sending SetupConnection (vendor=bitaxe, hw=%s)", device_model ? device_model : "");
            int frame_len = sv2_build_setup_connection(frame_buf, sizeof(frame_buf),
                                                       stratum_url, port,
                                                       "bitaxe", device_model ? device_model : "",
                                                       "", "");
            if (frame_len < 0 || sv2_noise_send(noise_ctx, transport, frame_buf, frame_len) != 0) {
                ESP_LOGE(TAG, "Failed to send SetupConnection");
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }
        }

        // 2. Receive SetupConnectionSuccess
        {
            if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf,
                               sizeof(recv_buf), &payload_len) != 0) {
                ESP_LOGE(TAG, "Failed to receive SetupConnectionSuccess");
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }
            sv2_parse_frame_header(hdr_buf, &hdr);

            if (hdr.msg_type != SV2_MSG_SETUP_CONNECTION_SUCCESS) {
                ESP_LOGE(TAG, "SetupConnection rejected by pool (msg_type=0x%02x)", hdr.msg_type);
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }

            uint16_t used_version;
            uint32_t flags;
            if (sv2_parse_setup_connection_success(recv_buf, payload_len, &used_version, &flags) != 0) {
                ESP_LOGE(TAG, "Failed to parse SetupConnectionSuccess");
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }
            ESP_LOGI(TAG, "Pool accepted connection: SV2 version=%d, flags=0x%08lx", used_version, flags);
        }

        // 3. Send OpenStandardMiningChannel
        {
            char *user = GLOBAL_STATE->SYSTEM_MODULE.pool_user;
            float hash_rate = 1e12; // 1 TH/s nominal (will be adjusted by pool via SetTarget)
            ESP_LOGI(TAG, "Opening mining channel (user=%s)", user ? user : "(empty)");
            int frame_len = sv2_build_open_standard_mining_channel(frame_buf, sizeof(frame_buf),
                                                                    1, user ? user : "", hash_rate);
            if (frame_len < 0 || sv2_noise_send(noise_ctx, transport, frame_buf, frame_len) != 0) {
                ESP_LOGE(TAG, "Failed to send OpenStandardMiningChannel");
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }
        }

        // 4. Receive OpenStandardMiningChannelSuccess
        {
            if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf,
                               sizeof(recv_buf), &payload_len) != 0) {
                ESP_LOGE(TAG, "Failed to receive OpenChannelSuccess");
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }
            sv2_parse_frame_header(hdr_buf, &hdr);

            if (hdr.msg_type != SV2_MSG_OPEN_STANDARD_MINING_CHANNEL_SUCCESS) {
                ESP_LOGE(TAG, "OpenChannel rejected by pool (msg_type=0x%02x)", hdr.msg_type);
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }

            uint32_t request_id, channel_id, group_channel_id;
            uint8_t target[32];
            uint8_t extranonce_prefix[32];
            uint8_t extranonce_prefix_len;

            if (sv2_parse_open_channel_success(recv_buf, payload_len,
                                                &request_id, &channel_id, target,
                                                extranonce_prefix, &extranonce_prefix_len,
                                                &group_channel_id) != 0) {
                ESP_LOGE(TAG, "Failed to parse OpenChannelSuccess");
                sv2_close_connection(GLOBAL_STATE);
                retry_attempts++;
                continue;
            }

            conn.channel_id = channel_id;
            conn.channel_opened = true;
            memcpy(conn.target, target, 32);

            uint32_t pdiff = sv2_target_to_pdiff(target);
            GLOBAL_STATE->pool_difficulty = pdiff;
            GLOBAL_STATE->new_set_mining_difficulty_msg = true;

            ESP_LOGI(TAG, "Mining channel opened: channel_id=%lu, group=%lu", channel_id, group_channel_id);
            ESP_LOGI(TAG, "Set pool difficulty: %lu", pdiff);
        }

        // Connection successful, reset retry counter
        retry_attempts = 0;

        {
            float elapsed_ms = (float)(esp_timer_get_time() - connect_start_us) / 1000.0f;
            ESP_LOGI(TAG, "SV2+Noise connection ready (%.0f ms). Waiting for jobs from %s:%d",
                     elapsed_ms, stratum_url, port);
        }

        // --- Main receive loop ---
        while (1) {
            // Read and decrypt frame
            if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf,
                               sizeof(recv_buf), &payload_len) != 0) {
                ESP_LOGE(TAG, "Failed to receive frame, reconnecting...");
                retry_attempts++;
                sv2_close_connection(GLOBAL_STATE);
                break;
            }

            sv2_parse_frame_header(hdr_buf, &hdr);

            // Dispatch by message type
            switch (hdr.msg_type) {
                case SV2_MSG_NEW_MINING_JOB:
                    sv2_handle_new_mining_job(GLOBAL_STATE, &conn, recv_buf, hdr.msg_length);
                    break;

                case SV2_MSG_SET_NEW_PREV_HASH:
                    sv2_handle_set_new_prev_hash(GLOBAL_STATE, &conn, recv_buf, hdr.msg_length);
                    break;

                case SV2_MSG_SET_TARGET:
                    sv2_handle_set_target(GLOBAL_STATE, &conn, recv_buf, hdr.msg_length);
                    break;

                case SV2_MSG_SUBMIT_SHARES_SUCCESS: {
                    uint32_t channel_id;
                    if (sv2_parse_submit_shares_success(recv_buf, hdr.msg_length, &channel_id) == 0) {
                        if (sv2_last_submit_time_us > 0) {
                            float response_time_ms = (float)(esp_timer_get_time() - sv2_last_submit_time_us) / 1000.0f;
                            ESP_LOGI(TAG, "Share accepted (%.1f ms)", response_time_ms);
                            GLOBAL_STATE->SYSTEM_MODULE.response_time = response_time_ms;
                        } else {
                            ESP_LOGI(TAG, "Share accepted");
                        }
                        SYSTEM_notify_accepted_share(GLOBAL_STATE);
                    }
                    break;
                }

                case SV2_MSG_SUBMIT_SHARES_ERROR: {
                    uint32_t channel_id, seq_num;
                    char error_code[64];
                    if (sv2_parse_submit_shares_error(recv_buf, hdr.msg_length,
                                                      &channel_id, &seq_num,
                                                      error_code, sizeof(error_code)) == 0) {
                        ESP_LOGW(TAG, "Share rejected: %s", error_code);
                        SYSTEM_notify_rejected_share(GLOBAL_STATE, error_code);
                    }
                    break;
                }

                default:
                    ESP_LOGW(TAG, "Unknown SV2 message type: 0x%02x (len=%lu)", hdr.msg_type, hdr.msg_length);
                    break;
            }
        }
    }

    vTaskDelete(NULL);
}
