#include "esp_log.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include <lwip/sockets.h>
#include "esp_timer.h"
#include "system.h"
#include "global_state.h"
#include "stratum_v2_client.h"
#include "stratum_task.h"
#include "stratum_socket.h"
#include "connect.h"
#include "sv2_protocol.h"
#include "sv2_noise.h"
#include "mining.h"
#include "utils.h"
#include "libbase58.h"
#include "device_config.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define TRANSPORT_TIMEOUT_MS 5000
#define SV2_MAX_FRAME_SIZE 8192

static const char *TAG = "stratum_v2";

static sv2_conn_t *s_v2_conn = NULL;

static bool add_active_job_id(uint32_t *active_job_ids, int *count, uint32_t job_id)
{
    for (int i = 0; i < *count; i++) {
        if (active_job_ids[i] == job_id) {
            return false;
        }
    }
    if (*count < SV2_MAX_ACTIVE_JOB_IDS) {
        active_job_ids[*count] = job_id;
        (*count)++;
    } else {
        for (int i = 1; i < SV2_MAX_ACTIVE_JOB_IDS; i++) {
            active_job_ids[i - 1] = active_job_ids[i];
        }
        active_job_ids[SV2_MAX_ACTIVE_JOB_IDS - 1] = job_id;
    }
    return true;
}

static void clear_active_job_ids(uint32_t *active_job_ids, int *count)
{
    memset(active_job_ids, 0, sizeof(uint32_t) * SV2_MAX_ACTIVE_JOB_IDS);
    *count = 0;
}

static void sv2_conn_free(sv2_conn_t **conn_ptr)
{
    if (!conn_ptr || !*conn_ptr) return;
    sv2_conn_t *c = *conn_ptr;
    if (c->noise_ctx) {
        sv2_noise_destroy(c->noise_ctx);
        c->noise_ctx = NULL;
    }
    clear_active_job_ids(c->active_job_ids, &c->active_job_ids_count);
    free(c);
    *conn_ptr = NULL;
}

static bool stratum_v2_load_authority_pubkey(uint8_t out[32], const char *b58_key)
{
    if (!b58_key || strlen(b58_key) == 0) {
        return false;
    }

    uint8_t decoded[64];
    size_t decoded_len = sizeof(decoded);

    if (!b58tobin(decoded, &decoded_len, b58_key, 0)) {
        ESP_LOGE(TAG, "Failed to decode base58 authority pubkey");
        return false;
    }

    if (decoded_len != 38) {
        ESP_LOGE(TAG, "Invalid decoded length: %zu (expected 38)", decoded_len);
        return false;
    }

    uint8_t *data = decoded + (sizeof(decoded) - decoded_len);

    if (data[0] != 0x01 || data[1] != 0x00) {
        ESP_LOGE(TAG, "Invalid key version: 0x%02x%02x (expected 0x0100)", data[1], data[0]);
        return false;
    }

    memcpy(out, data + 2, 32);
    ESP_LOGI(TAG, "Successfully decoded base58 authority pubkey");
    return true;
}

void stratum_v2_close_connection(GlobalState *GLOBAL_STATE)
{
    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    GLOBAL_STATE->transport = NULL;
    sv2_conn_t *conn = s_v2_conn;
    s_v2_conn = NULL;

    if (conn && conn->noise_ctx) {
        sv2_noise_destroy(conn->noise_ctx);
        conn->noise_ctx = NULL;
    }
    if (transport != NULL) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
    }
    if (conn != NULL) {
        clear_active_job_ids(conn->active_job_ids, &conn->active_job_ids_count);
        free(conn);
    }
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);

    GLOBAL_STATE->SYSTEM_MODULE.shares_pending = 0;
    SYSTEM_clean_jobs_queue(GLOBAL_STATE);
    SYSTEM_reset_coinbase_ui_state(GLOBAL_STATE, "");
}

#define SV2_SUBMIT_TIMING_SLOTS 32
static int64_t stratum_v2_submit_time_us[SV2_SUBMIT_TIMING_SLOTS] = {0};

static void stratum_v2_update_pending_shares(GlobalState *GLOBAL_STATE)
{
    sv2_conn_t *conn = s_v2_conn;
    if (!conn) {
        GLOBAL_STATE->SYSTEM_MODULE.shares_pending = 0;
        return;
    }
    uint32_t pending = (conn->sequence_number > conn->resolved_shares)
                           ? (conn->sequence_number - conn->resolved_shares)
                           : 0;
    GLOBAL_STATE->SYSTEM_MODULE.shares_pending = (uint16_t)(pending > UINT16_MAX ? UINT16_MAX : pending);
}

static void stratum_v2_track_submit(GlobalState *GLOBAL_STATE, uint32_t sequence_number)
{
    stratum_v2_submit_time_us[sequence_number % SV2_SUBMIT_TIMING_SLOTS] = esp_timer_get_time();
    stratum_v2_update_pending_shares(GLOBAL_STATE);
}

int stratum_v2_submit_share(GlobalState *GLOBAL_STATE, const bm_job *active_job,
                            uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us)
{
    if (!GLOBAL_STATE || !active_job) {
        return -1;
    }

    uint8_t extranonce_2[32];
    uint8_t en2_len = 0;

    if (active_job->job_type == JOB_TYPE_SV2_EXTENDED && active_job->extranonce2) {
        en2_len = (uint8_t)(strlen(active_job->extranonce2) / 2);
        if (en2_len > sizeof(extranonce_2)) en2_len = sizeof(extranonce_2);
        hex2bin(active_job->extranonce2, extranonce_2, en2_len);
    }

    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    esp_transport_handle_t transport = GLOBAL_STATE->transport;
    sv2_conn_t *conn = s_v2_conn;

    if (!transport || !conn || !conn->noise_ctx) {
        pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
        return -1;
    }

    uint32_t sequence_number = conn->sequence_number++;
    uint8_t buf[SV2_SUBMIT_SHARES_MAX_FRAME_SIZE];

    uint32_t sv2_job_id = (uint32_t)strtoul(active_job->jobid, NULL, 10);
    int len = sv2_build_submit_shares(buf, sizeof(buf),
                                      conn->channel_id,
                                      sequence_number,
                                      sv2_job_id, nonce, active_job->ntime, rolled_version,
                                      en2_len > 0 ? extranonce_2 : NULL, en2_len);
    if (len < 0) {
        pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
        return -1;
    }

    int ret = sv2_noise_send(conn->noise_ctx, transport, buf, len);
    if (ret >= 0) {
        stratum_v2_track_submit(GLOBAL_STATE, sequence_number);
        if (sent_time_us) {
            *sent_time_us = esp_timer_get_time();
        }
    }
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);
    return ret;
}

static void stratum_v2_handle_new_extended_mining_job(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                                       const uint8_t *payload, uint32_t len)
{
    if (len < 8) {
        ESP_LOGE(TAG, "NewExtendedMiningJob payload too short (%lu bytes)", (unsigned long)len);
        return;
    }

    uint32_t job_id = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8) |
                      ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 24);
    uint8_t slot = (uint8_t)(job_id % MINER_JOB_POOL_SIZE);
    miner_job_t *job = miner_job_get_slot(slot);
    uint32_t channel_id = 0;
    bool has_min_ntime = false;
    bool version_rolling_allowed = false;

    if (sv2_parse_new_extended_mining_job(payload, len, &channel_id, job, &has_min_ntime, &version_rolling_allowed) != 0) {
        ESP_LOGE(TAG, "Failed to parse NewExtendedMiningJob");
        conn->pending_jobs_valid &= ~(1U << slot);
        return;
    }

    if (!sv2_channel_or_group_matches(channel_id, conn->channel_id, conn->group_channel_id)) {
        ESP_LOGW(TAG, "Dropping NewExtendedMiningJob for unexpected channel %lu (expected %lu or group %lu)",
                 (unsigned long)channel_id, (unsigned long)conn->channel_id, (unsigned long)conn->group_channel_id);
        conn->pending_jobs_valid &= ~(1U << slot);
        return;
    }

    job->pool_id = conn->pool_idx;
    job->pool_diff = hash_to_pdiff(conn->target);
    job->version_mask = version_rolling_allowed ? conn->version_mask : 0;
    job->extranonce1_len = conn->extranonce_prefix_len;
    if (job->extranonce1_len > sizeof(job->extranonce1)) job->extranonce1_len = sizeof(job->extranonce1);
    if (job->extranonce1_len > 0) {
        memcpy(job->extranonce1, conn->extranonce_prefix, job->extranonce1_len);
    }
    job->extranonce2_len = conn->extranonce_size;

    conn->pending_jobs_valid |= (1U << slot);

    if (has_min_ntime && conn->has_prev_hash) {
        if (!add_active_job_id(conn->active_job_ids, &conn->active_job_ids_count, job_id)) {
            ESP_LOGW(TAG, "Ignoring duplicate V2 job %s", job->job_id);
            return;
        }
        memcpy(job->prev_hash, conn->prev_hash, 32);
        job->nbits = conn->prev_hash_nbits;
        job->clean_jobs = false;

        GLOBAL_STATE->SYSTEM_MODULE.work_received++;
        SYSTEM_notify_new_ntime(GLOBAL_STATE, job->ntime);
        if (GLOBAL_STATE->create_jobs_task_handle) {
            xTaskNotify(GLOBAL_STATE->create_jobs_task_handle, slot, eSetValueWithOverwrite);
        }
    }
}

static void stratum_v2_handle_new_mining_job(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                             const uint8_t *payload, uint32_t len)
{
    uint32_t channel_id, job_id, version, min_ntime;
    uint8_t merkle_root[32];
    bool has_min_ntime = false;

    if (sv2_parse_new_mining_job(payload, len, &channel_id, &job_id,
                                 &has_min_ntime, &min_ntime,
                                 &version, merkle_root) != 0) {
        ESP_LOGE(TAG, "Failed to parse NewMiningJob");
        return;
    }

    if (!sv2_channel_or_group_matches(channel_id, conn->channel_id, conn->group_channel_id)) {
        ESP_LOGW(TAG, "Dropping NewMiningJob for unexpected channel %lu (expected %lu or group %lu)",
                 (unsigned long)channel_id, (unsigned long)conn->channel_id, (unsigned long)conn->group_channel_id);
        return;
    }

    uint8_t slot = (uint8_t)(job_id % MINER_JOB_POOL_SIZE);
    miner_job_t *job = miner_job_get_slot(slot);
    uint8_t *p_buf = job->coinbase_prefix;
    uint8_t *s_buf = job->coinbase_suffix;
    memset(job, 0, sizeof(miner_job_t));
    job->coinbase_prefix = p_buf;
    job->coinbase_suffix = s_buf;

    job->type = JOB_TYPE_SV2_STANDARD;
    snprintf(job->job_id, sizeof(job->job_id), "%lu", (unsigned long)job_id);
    job->version = version;
    memcpy(job->merkle_root, merkle_root, 32);
    job->pool_id = conn->pool_idx;
    job->pool_diff = hash_to_pdiff(conn->target);
    job->version_mask = conn->version_mask;
    conn->pending_jobs_valid |= (1U << slot);

    if (has_min_ntime && conn->has_prev_hash) {
        if (!add_active_job_id(conn->active_job_ids, &conn->active_job_ids_count, job_id)) {
            ESP_LOGW(TAG, "Ignoring duplicate V2 job %s", job->job_id);
            return;
        }
        memcpy(job->prev_hash, conn->prev_hash, 32);
        job->ntime = min_ntime;
        job->nbits = conn->prev_hash_nbits;
        job->clean_jobs = false;

        GLOBAL_STATE->SYSTEM_MODULE.work_received++;
        SYSTEM_notify_new_ntime(GLOBAL_STATE, job->ntime);
        if (GLOBAL_STATE->create_jobs_task_handle) {
            xTaskNotify(GLOBAL_STATE->create_jobs_task_handle, slot, eSetValueWithOverwrite);
        }
    }
}

static void stratum_v2_handle_set_new_prev_hash(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                                const uint8_t *payload, uint32_t len)
{
    uint32_t channel_id, job_id, min_ntime, nbits;
    uint8_t prev_hash[32];

    if (sv2_parse_set_new_prev_hash(payload, len, &channel_id, &job_id,
                                    prev_hash, &min_ntime, &nbits) != 0) {
        ESP_LOGE(TAG, "Failed to parse SetNewPrevHash");
        return;
    }

    if (!sv2_channel_or_group_matches(channel_id, conn->channel_id, conn->group_channel_id)) {
        ESP_LOGW(TAG, "Dropping SetNewPrevHash for unexpected channel %lu (expected %lu or group %lu)",
                 (unsigned long)channel_id, (unsigned long)conn->channel_id, (unsigned long)conn->group_channel_id);
        return;
    }

    memcpy(conn->prev_hash, prev_hash, 32);
    conn->prev_hash_ntime = min_ntime;
    conn->prev_hash_nbits = nbits;
    conn->has_prev_hash = true;

    uint8_t slot = (uint8_t)(job_id % MINER_JOB_POOL_SIZE);
    miner_job_t *job = miner_job_get_slot(slot);

    if ((conn->pending_jobs_valid & (1U << slot)) &&
        (uint32_t)strtoul(job->job_id, NULL, 10) == job_id) {
        clear_active_job_ids(conn->active_job_ids, &conn->active_job_ids_count);
        add_active_job_id(conn->active_job_ids, &conn->active_job_ids_count, job_id);

        memcpy(job->prev_hash, prev_hash, 32);
        job->ntime = min_ntime;
        job->nbits = nbits;
        job->clean_jobs = true;
        job->pool_id = conn->pool_idx;
        job->pool_diff = hash_to_pdiff(conn->target);
        if (job->type == JOB_TYPE_SV2_STANDARD) {
            job->version_mask = conn->version_mask;
        } else if (job->type == JOB_TYPE_SV2_EXTENDED) {
            job->extranonce1_len = conn->extranonce_prefix_len;
            if (job->extranonce1_len > sizeof(job->extranonce1)) job->extranonce1_len = sizeof(job->extranonce1);
            if (job->extranonce1_len > 0) {
                memcpy(job->extranonce1, conn->extranonce_prefix, job->extranonce1_len);
            }
            job->extranonce2_len = conn->extranonce_size;
        }

        GLOBAL_STATE->SYSTEM_MODULE.work_received++;
        SYSTEM_notify_new_ntime(GLOBAL_STATE, job->ntime);
        if (GLOBAL_STATE->create_jobs_task_handle) {
            xTaskNotify(GLOBAL_STATE->create_jobs_task_handle, slot, eSetValueWithOverwrite);
        }
    } else {
        ESP_LOGW(TAG, "SetNewPrevHash for unknown job_id %lu", (unsigned long)job_id);
    }
}

static void stratum_v2_handle_set_target(GlobalState *GLOBAL_STATE, sv2_conn_t *conn,
                                         const uint8_t *payload, uint32_t len)
{
    uint32_t channel_id;
    uint8_t max_target[32];

    if (sv2_parse_set_target(payload, len, &channel_id, max_target) != 0) {
        ESP_LOGE(TAG, "Failed to parse SetTarget");
        return;
    }

    if (!sv2_channel_or_group_matches(channel_id, conn->channel_id, conn->group_channel_id)) {
        ESP_LOGW(TAG, "Dropping SetTarget for unexpected channel %lu (expected %lu or group %lu)",
                 (unsigned long)channel_id, (unsigned long)conn->channel_id, (unsigned long)conn->group_channel_id);
        return;
    }

    double pdiff = hash_to_pdiff(max_target);
    if (isnan(pdiff) || isinf(pdiff) || pdiff < 0.0001 || pdiff > 4294967295.0) {
        ESP_LOGW(TAG, "Ignoring out-of-range SV2 target pdiff: %g", pdiff);
        return;
    }

    memcpy(conn->target, max_target, 32);
    GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty = pdiff;

    ESP_LOGI(TAG, "Set pool difficulty: %g", pdiff);
}

esp_err_t stratum_v2_run(GlobalState *GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE || pool_idx >= MAX_POOLS) {
        return ESP_ERR_INVALID_ARG;
    }

    PoolConfig *pool = &GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx];
    char stratum_url[256] = {0};
    char user[256] = {0};
    char auth_pubkey[128] = {0};

    if (pool->url) strlcpy(stratum_url, pool->url, sizeof(stratum_url));
    if (pool->user) strlcpy(user, pool->user, sizeof(user));
    if (pool->sv2_authority_pubkey) strlcpy(auth_pubkey, pool->sv2_authority_pubkey, sizeof(auth_pubkey));

    uint16_t port = pool->port;
    sv2_channel_type_t channel_type = pool->sv2_channel_type;
    bool require_auth = pool->sv2_require_auth;

    if (stratum_url[0] == '\0' || port == 0) {
        ESP_LOGE(TAG, "Invalid pool configuration for pool %u", pool_idx);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_v2_conn != NULL) {
        sv2_conn_free(&s_v2_conn);
    }

    sv2_conn_t *conn = heap_caps_calloc(1, sizeof(sv2_conn_t), MALLOC_CAP_SPIRAM);
    if (!conn) {
        conn = calloc(1, sizeof(sv2_conn_t));
    }
    if (!conn) {
        ESP_LOGE(TAG, "Failed to allocate sv2_conn");
        return ESP_ERR_NO_MEM;
    }
    conn->pool_idx = (uint8_t)pool_idx;
    conn->version_mask = BIP320_VERSION_ROLLING_MASK;
    s_v2_conn = conn;

    uint8_t *frame_buf = heap_caps_malloc(SV2_MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    uint8_t *recv_buf = heap_caps_malloc(SV2_MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM);

    if (!frame_buf || !recv_buf) {
        ESP_LOGE(TAG, "Failed to allocate frame buffers");
        free(frame_buf);
        free(recv_buf);
        sv2_conn_free(&s_v2_conn);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Connecting to stratum+sv2://%s:%d", stratum_url, port);

    esp_transport_handle_t transport = esp_transport_tcp_init();
    if (!transport) {
        ESP_LOGE(TAG, "Failed to init TCP transport");
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Internal error");
        free(frame_buf);
        free(recv_buf);
        sv2_conn_free(&s_v2_conn);
        return ESP_FAIL;
    }

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(stratum_url, port, &conn_info) != ESP_OK) {
        ESP_LOGE(TAG, "Address resolution failed for %s", stratum_url);
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Pool unreachable");
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        free(frame_buf);
        free(recv_buf);
        sv2_conn_free(&s_v2_conn);
        return ESP_FAIL;
    }

    int64_t connect_start_us = esp_timer_get_time();

    esp_err_t ret = esp_transport_connect(transport, conn_info.host_ip, port, TRANSPORT_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCP connect failed to %s:%d (%s) (err %d)", stratum_url, port, conn_info.host_ip, ret);
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Pool unreachable");
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        free(frame_buf);
        free(recv_buf);
        sv2_conn_free(&s_v2_conn);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TCP connected to %s:%d (%s)", stratum_url, port, conn_info.host_ip);

    pthread_mutex_lock(&GLOBAL_STATE->transport_mutex);
    GLOBAL_STATE->transport = transport;
    pthread_mutex_unlock(&GLOBAL_STATE->transport_mutex);

    stratum_socket_set_options(transport);

    stratum_v2_update_pending_shares(GLOBAL_STATE);

    sv2_noise_ctx_t *noise_ctx = sv2_noise_create();
    if (!noise_ctx) {
        ESP_LOGE(TAG, "Failed to create noise context");
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Internal error");
        stratum_v2_close_connection(GLOBAL_STATE);
        free(frame_buf);
        free(recv_buf);
        return ESP_FAIL;
    }
    conn->noise_ctx = noise_ctx;

    uint8_t auth_key[32];
    bool has_auth = stratum_v2_load_authority_pubkey(auth_key, auth_pubkey[0] ? auth_pubkey : NULL);

    if (require_auth && !has_auth) {
        ESP_LOGE(TAG, "SV2 authentication required but no authority pubkey configured, refusing to connect");
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Auth required - no key");
        stratum_v2_close_connection(GLOBAL_STATE);
        free(frame_buf);
        free(recv_buf);
        return ESP_FAIL;
    }

    if (has_auth) {
        ESP_LOGI(TAG, "Authority pubkey configured, will verify server certificate");
    } else {
        ESP_LOGW(TAG, "No authority pubkey configured, server identity will not be verified");
    }

    if (sv2_noise_handshake(noise_ctx, transport, has_auth ? auth_key : NULL) != 0) {
        ESP_LOGE(TAG, "Noise handshake failed, reconnecting...");
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Auth failed - check key");
        stratum_v2_close_connection(GLOBAL_STATE);
        free(frame_buf);
        free(recv_buf);
        return ESP_FAIL;
    }

    const char *ip_protocol = (conn_info.addr_family == AF_INET6) ? "IPv6" : "IPv4";
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "%s", ip_protocol);

    ESP_LOGI(TAG, "Encrypted channel established (ChaCha20-Poly1305)");

    uint8_t hdr_buf[6];
    sv2_frame_header_t hdr;
    int payload_len;

    conn->channel_type = channel_type;
    uint32_t setup_flags = sv2_setup_flags_for_channel(channel_type);

    // 1. Send SetupConnection
    {
        const char *device_model = GLOBAL_STATE->DEVICE_CONFIG.family.asic.name;
        ESP_LOGI(TAG, "Sending SetupConnection (vendor=bitaxe, hw=%s, channel=%s)",
                 device_model ? device_model : "",
                 sv2_channel_type_to_string(channel_type));
        int frame_len = sv2_build_setup_connection(frame_buf, SV2_MAX_FRAME_SIZE,
                                                   stratum_url, port,
                                                   "bitaxe", device_model ? device_model : "",
                                                   "", "", setup_flags);
        if (frame_len < 0 || sv2_noise_send(noise_ctx, transport, frame_buf, frame_len) != 0) {
            ESP_LOGE(TAG, "Failed to send SetupConnection");
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Connection lost");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }
    }

    // 2. Receive SetupConnectionSuccess
    {
        if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf,
                           SV2_MAX_FRAME_SIZE, &payload_len) != 0) {
            ESP_LOGE(TAG, "Failed to receive SetupConnectionSuccess");
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Pool not responding");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }
        sv2_parse_frame_header(hdr_buf, &hdr);

        if (hdr.msg_type != SV2_MSG_SETUP_CONNECTION_SUCCESS) {
            ESP_LOGE(TAG, "SetupConnection rejected by pool (msg_type=0x%02x)", hdr.msg_type);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Pool rejected config");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }

        uint16_t used_version;
        uint32_t flags;
        if (sv2_parse_setup_connection_success(recv_buf, payload_len, &used_version, &flags) != 0) {
            ESP_LOGE(TAG, "Failed to parse SetupConnectionSuccess");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }

        if (!sv2_setup_success_allows_version_rolling(flags)) {
            ESP_LOGE(TAG, "Pool requires fixed version, but miner hardware requires version rolling");
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Fixed version unsupported");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Pool accepted connection: SV2 version=%d, flags=0x%08lx", used_version, flags);
    }

    // 3. Send OpenMiningChannel
    {
        float expected_gh = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate;
        float hash_rate = (expected_gh > 0.0f) ? (expected_gh * 1e9f) : 1e12f;
        int frame_len;

        if (channel_type == SV2_CHANNEL_EXTENDED) {
            ESP_LOGI(TAG, "Opening extended mining channel (user=%s)", user[0] ? user : "(empty)");
            frame_len = sv2_build_open_extended_mining_channel(frame_buf, SV2_MAX_FRAME_SIZE,
                                                                1, user, hash_rate, 2);
        } else {
            ESP_LOGI(TAG, "Opening standard mining channel (user=%s)", user[0] ? user : "(empty)");
            frame_len = sv2_build_open_standard_mining_channel(frame_buf, SV2_MAX_FRAME_SIZE,
                                                                1, user, hash_rate);
        }

        if (frame_len < 0 || sv2_noise_send(noise_ctx, transport, frame_buf, frame_len) != 0) {
            ESP_LOGE(TAG, "Failed to send OpenMiningChannel");
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Connection lost");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }
    }

    // 4. Receive OpenMiningChannelSuccess
    {
        if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf,
                           SV2_MAX_FRAME_SIZE, &payload_len) != 0) {
            ESP_LOGE(TAG, "Failed to receive OpenChannelSuccess");
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Pool not responding");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }
        sv2_parse_frame_header(hdr_buf, &hdr);

        uint8_t expected_msg = (channel_type == SV2_CHANNEL_EXTENDED)
                               ? SV2_MSG_OPEN_EXTENDED_MINING_CHANNEL_SUCCESS
                               : SV2_MSG_OPEN_STANDARD_MINING_CHANNEL_SUCCESS;

        if (hdr.msg_type != expected_msg) {
            ESP_LOGE(TAG, "OpenChannel rejected by pool (msg_type=0x%02x, expected=0x%02x)",
                     hdr.msg_type, expected_msg);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info,
                     sizeof(GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info), "SV2: Pool rejected miner");
            stratum_v2_close_connection(GLOBAL_STATE);
            free(frame_buf);
            free(recv_buf);
            return ESP_FAIL;
        }

        uint32_t request_id, channel_id, group_channel_id;
        uint8_t target[32];

        if (channel_type == SV2_CHANNEL_EXTENDED) {
            uint16_t extranonce_size;
            uint8_t extranonce_prefix[32];
            uint8_t extranonce_prefix_len;

            if (sv2_parse_open_extended_channel_success(recv_buf, payload_len,
                                                        &request_id, &channel_id, target,
                                                        &extranonce_size,
                                                        extranonce_prefix, &extranonce_prefix_len,
                                                        &group_channel_id) != 0) {
                ESP_LOGE(TAG, "Failed to parse OpenExtendedChannelSuccess");
                stratum_v2_close_connection(GLOBAL_STATE);
                free(frame_buf);
                free(recv_buf);
                return ESP_FAIL;
            }

            conn->extranonce_size = (uint8_t)extranonce_size;
            conn->extranonce_prefix_len = extranonce_prefix_len;
            memcpy(conn->extranonce_prefix, extranonce_prefix, extranonce_prefix_len);

            ESP_LOGI(TAG, "Extended channel: extranonce_size=%d, prefix_len=%d",
                     extranonce_size, extranonce_prefix_len);
        } else {
            uint8_t extranonce_prefix[32];
            uint8_t extranonce_prefix_len;

            if (sv2_parse_open_channel_success(recv_buf, payload_len,
                                                &request_id, &channel_id, target,
                                                extranonce_prefix, &extranonce_prefix_len,
                                                &group_channel_id) != 0) {
                ESP_LOGE(TAG, "Failed to parse OpenChannelSuccess");
                stratum_v2_close_connection(GLOBAL_STATE);
                free(frame_buf);
                free(recv_buf);
                return ESP_FAIL;
            }
        }

        conn->channel_id = channel_id;
        conn->group_channel_id = group_channel_id;
        conn->has_group_channel = (group_channel_id != 0);
        conn->channel_opened = true;
        memcpy(conn->target, target, 32);

        double pdiff = hash_to_pdiff(target);
        GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty = pdiff;

        ESP_LOGI(TAG, "Mining channel opened: channel_id=%lu, group=%lu, type=%s",
                 channel_id, group_channel_id,
                 sv2_channel_type_to_string(channel_type));
        ESP_LOGI(TAG, "Set pool difficulty: %g", pdiff);
    }

    {
        float elapsed_ms = (float)(esp_timer_get_time() - connect_start_us) / 1000.0f;
        ESP_LOGI(TAG, "SV2+Noise connection ready (%.0f ms). Waiting for jobs from %s:%d",
                 elapsed_ms, stratum_url, port);
    }

    esp_err_t run_result = ESP_OK;

    while (1) {
        if (stratum_reconnect_requested()) {
            ESP_LOGI(TAG, "Reconnect requested");
            run_result = ESP_OK;
            break;
        }

        if (GLOBAL_STATE->SYSTEM_MODULE.mining_paused || GLOBAL_STATE->SYSTEM_MODULE.hardware_fault) {
            ESP_LOGI(TAG, "Mining paused, disconnecting from SV2 pool");
            run_result = ESP_OK;
            break;
        }

        if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf,
                           SV2_MAX_FRAME_SIZE, &payload_len) != 0) {
            if (stratum_reconnect_requested()) {
                ESP_LOGI(TAG, "Reconnect requested during recv");
                run_result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to receive frame, reconnecting...");
                run_result = ESP_FAIL;
            }
            break;
        }

        sv2_parse_frame_header(hdr_buf, &hdr);

        switch (hdr.msg_type) {
            // Job messages must match the channel we negotiated. Consumers pick
            // the job struct type from the channel type, so accepting the wrong
            // message here would have them read and free the wrong type.
            case SV2_MSG_NEW_MINING_JOB:
                if (conn->channel_type != SV2_CHANNEL_STANDARD) {
                    ESP_LOGW(TAG, "Ignoring NewMiningJob received on an extended channel");
                    break;
                }
                stratum_v2_handle_new_mining_job(GLOBAL_STATE, conn, recv_buf, hdr.msg_length);
                break;

            case SV2_MSG_NEW_EXTENDED_MINING_JOB:
                if (conn->channel_type != SV2_CHANNEL_EXTENDED) {
                    ESP_LOGW(TAG, "Ignoring NewExtendedMiningJob received on a standard channel");
                    break;
                }
                stratum_v2_handle_new_extended_mining_job(GLOBAL_STATE, conn, recv_buf, hdr.msg_length);
                break;

            case SV2_MSG_SET_NEW_PREV_HASH:
                stratum_v2_handle_set_new_prev_hash(GLOBAL_STATE, conn, recv_buf, hdr.msg_length);
                break;

            case SV2_MSG_SET_TARGET:
                stratum_v2_handle_set_target(GLOBAL_STATE, conn, recv_buf, hdr.msg_length);
                break;

            case SV2_MSG_SUBMIT_SHARES_SUCCESS: {
                uint32_t channel_id, last_sequence_number, accepted_count;
                if (sv2_parse_submit_shares_success(recv_buf, hdr.msg_length, &channel_id, &last_sequence_number, &accepted_count) == 0) {
                    if (channel_id != conn->channel_id) {
                        ESP_LOGW(TAG, "Dropping SubmitSharesSuccess for unexpected channel %lu (expected %lu)",
                                 (unsigned long)channel_id, (unsigned long)conn->channel_id);
                        break;
                    }
                    uint32_t pending = (conn->sequence_number > conn->resolved_shares)
                                           ? (conn->sequence_number - conn->resolved_shares)
                                           : 0;
                    if (accepted_count > pending) {
                        ESP_LOGW(TAG, "Clamping accepted_count (%lu) to pending shares (%lu)",
                                 (unsigned long)accepted_count, (unsigned long)pending);
                        accepted_count = pending;
                    }
                    int slot = last_sequence_number % SV2_SUBMIT_TIMING_SLOTS;
                    int64_t submit_time_us = stratum_v2_submit_time_us[slot];
                    if (submit_time_us > 0) {
                        float response_time_ms = (float)(esp_timer_get_time() - submit_time_us) / 1000.0f;
                        ESP_LOGI(TAG, "Shares accepted: %lu (%.1f ms)", accepted_count, response_time_ms);
                        GLOBAL_STATE->SYSTEM_MODULE.response_time = response_time_ms;
                        GLOBAL_STATE->SYSTEM_MODULE.response_share_batch = (uint16_t)accepted_count;
                        stratum_v2_submit_time_us[slot] = 0;
                    } else {
                        ESP_LOGI(TAG, "Shares accepted: %lu", accepted_count);
                    }
                    for (uint32_t i = 0; i < accepted_count; i++) {
                        SYSTEM_notify_accepted_share(GLOBAL_STATE);
                    }
                    uint32_t resolved = last_sequence_number + 1;
                    if (resolved > conn->resolved_shares) {
                        conn->resolved_shares = resolved;
                    }
                    stratum_v2_update_pending_shares(GLOBAL_STATE);
                }
                break;
            }

            case SV2_MSG_SUBMIT_SHARES_ERROR: {
                uint32_t channel_id, seq_num;
                char error_code[64];
                if (sv2_parse_submit_shares_error(recv_buf, hdr.msg_length,
                                                  &channel_id, &seq_num,
                                                  error_code, sizeof(error_code)) == 0) {
                    if (channel_id != conn->channel_id) {
                        ESP_LOGW(TAG, "Dropping SubmitSharesError for unexpected channel %lu (expected %lu)",
                                 (unsigned long)channel_id, (unsigned long)conn->channel_id);
                        break;
                    }
                    ESP_LOGW(TAG, "Share rejected: %s", error_code);
                    SYSTEM_notify_rejected_share(GLOBAL_STATE, error_code);
                    uint32_t resolved = seq_num + 1;
                    if (resolved > conn->resolved_shares) {
                        conn->resolved_shares = resolved;
                    }
                    stratum_v2_update_pending_shares(GLOBAL_STATE);
                }
                break;
            }

            default:
                ESP_LOGW(TAG, "Unknown SV2 message type: 0x%02x (len=%lu)", hdr.msg_type, hdr.msg_length);
                break;
        }
    }

    stratum_v2_close_connection(GLOBAL_STATE);
    free(frame_buf);
    free(recv_buf);
    return run_result;
}

bool stratum_v2_probe_pool(GlobalState *GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE || pool_idx >= MAX_POOLS) return false;

    PoolConfig *pool = &GLOBAL_STATE->SYSTEM_MODULE.pools[pool_idx];
    char url[256] = {0};
    char auth_pubkey[128] = {0};

    if (pool->url) strlcpy(url, pool->url, sizeof(url));
    if (pool->sv2_authority_pubkey) strlcpy(auth_pubkey, pool->sv2_authority_pubkey, sizeof(auth_pubkey));

    uint16_t port = pool->port;
    sv2_channel_type_t channel_type = pool->sv2_channel_type;
    bool require_auth = pool->sv2_require_auth;

    if (url[0] == '\0' || port == 0) return false;

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(url, port, &conn_info) != ESP_OK) {
        return false;
    }

    esp_transport_handle_t transport = esp_transport_tcp_init();
    if (!transport) return false;

    esp_err_t ret = esp_transport_connect(transport, conn_info.host_ip, port, TRANSPORT_TIMEOUT_MS);
    if (ret != ESP_OK) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    stratum_socket_set_options(transport);

    sv2_noise_ctx_t *noise_ctx = sv2_noise_create();
    if (!noise_ctx) {
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    uint8_t auth_key[32];
    bool has_auth = stratum_v2_load_authority_pubkey(auth_key, auth_pubkey[0] ? auth_pubkey : NULL);

    if (require_auth && !has_auth) {
        sv2_noise_destroy(noise_ctx);
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    if (sv2_noise_handshake(noise_ctx, transport, has_auth ? auth_key : NULL) != 0) {
        sv2_noise_destroy(noise_ctx);
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    uint8_t *frame_buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (!frame_buf) frame_buf = malloc(1024);
    uint8_t *recv_buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (!recv_buf) recv_buf = malloc(1024);

    if (!frame_buf || !recv_buf) {
        free(frame_buf);
        free(recv_buf);
        sv2_noise_destroy(noise_ctx);
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return false;
    }

    uint32_t setup_flags = sv2_setup_flags_for_channel(channel_type);

    const char *device_model = GLOBAL_STATE->DEVICE_CONFIG.family.asic.name;
    int frame_len = sv2_build_setup_connection(frame_buf, 1024,
                                               url, port,
                                               "bitaxe", device_model ? device_model : "",
                                               "", "", setup_flags);

    bool success = false;
    if (frame_len > 0 && sv2_noise_send(noise_ctx, transport, frame_buf, frame_len) == 0) {
        uint8_t hdr_buf[6];
        int payload_len = 0;
        if (sv2_noise_recv(noise_ctx, transport, hdr_buf, recv_buf, 1024, &payload_len) == 0) {
            sv2_frame_header_t hdr;
            if (sv2_parse_frame_header(hdr_buf, &hdr) == 0 && hdr.msg_type == SV2_MSG_SETUP_CONNECTION_SUCCESS) {
                uint16_t used_version;
                uint32_t flags;
                if (sv2_parse_setup_connection_success(recv_buf, payload_len, &used_version, &flags) == 0) {
                    if (sv2_setup_success_allows_version_rolling(flags)) {
                        success = true;
                    }
                }
            }
        }
    }

    free(frame_buf);
    free(recv_buf);
    sv2_noise_destroy(noise_ctx);
    esp_transport_close(transport);
    esp_transport_destroy(transport);
    return success;
}
