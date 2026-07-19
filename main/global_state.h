#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"
#include "power_management_task.h"
#include "hashrate_monitor_task.h"
#include "mining.h"
#include "coinbase_decoder.h"
#include "work_queue.h"
#include "device_config.h"
#include "display.h"
#include "scoreboard.h"
#include "esp_transport.h"

typedef enum {
    STRATUM_PROTOCOL_UNKNOWN = 0,
    STRATUM_PROTOCOL_V1 = 1,
    STRATUM_PROTOCOL_V2 = 2,
} stratum_protocol_t;

#define STRATUM_V1 "SV1"
#define STRATUM_V2 "SV2"

// Forward declarations
struct sv2_conn;
struct sv2_noise_ctx;

#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

typedef struct {
    char * url;
    char * user;
    char * pass;
    stratum_protocol_t protocol;
    uint16_t port;
    uint16_t difficulty;
    uint16_t tls;
    uint16_t sv2_channel_type;
    char * cert;
    char * sv2_authority_pubkey;
    bool decode_coinbase_tx;
    bool extranonce_subscribe;
} PoolConfig;

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10
#define MAX_BLOCK_SIGNALS 8
#define MAX_BLOCK_SIGNAL_LEN 16
#define MAX_POOLS 8

typedef struct {
    char message[64];
    uint32_t count;
} RejectedReasonStat;

typedef struct
{
    int64_t start_time;
    float current_hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float error_percentage;
    float response_time;
    float process_time;
    float cpu_usage;
    uint16_t power_fault;
    uint16_t response_share_batch;
    uint32_t lastClockSync;
    uint32_t shares_accepted;
    uint32_t shares_rejected;
    uint32_t work_received;
    int rejected_reason_stats_count;
    int screen_page;
    char * ssid;
    uint64_t best_nonce_diff;
    uint64_t best_session_nonce_diff;
    char best_diff_string[DIFF_STRING_SIZE];
    char best_session_diff_string[DIFF_STRING_SIZE];
    char pool_connection_info[64];
    RejectedReasonStat rejected_reason_stats[10];
    Scoreboard scoreboard;
    PoolConfig pools[MAX_POOLS];
    uint16_t primary_pool_index;
    uint16_t secondary_pool_index;
    int identify_mode_time_ms;
    int block_found;
    bool show_new_block;
    bool ap_enabled;
    bool is_connected;
    bool use_fallback_stratum;
    bool is_using_fallback;
    bool overheat_mode;
    bool mining_paused;
    bool pools_unavailable;
    bool is_screen_active;
    bool is_firmware_update;
    char firmware_update_filename[20];
    char firmware_update_status[20];
    bool hardware_fault;
    char hardware_fault_msg[64];
    const char * asic_status;
    char * version;
    char * axeOSVersion;
    char mdns_hostname[64];
    char full_hostname[70];
    char wifi_status[256];
    char ipv6_addr_str[64]; // IPv6 address string with zone identifier (INET6_ADDRSTRLEN=46 + % + interface=15)
    char ip_addr_str[16]; // IP4ADDR_STRLEN_MAX
    char ap_ssid[12];
} SystemModule;

typedef struct
{
    uint64_t accepted_count;
    uint64_t rejected_count;
    double hashes;
    bool is_active;
    pthread_mutex_t lock;
} SelfTestNonceMeasurement;

typedef struct
{
    bool is_active;
    bool is_finished;
    SelfTestNonceMeasurement nonce_measurement;
    const char *message;
    char *result;
    char *finished;
    esp_err_t system_init_ret;
} SelfTestModule;

typedef struct
{
    // ASIC may not return the nonce in the same order as the jobs were sent
    // it also may return a previous nonce under some circumstances
    // so we keep a list of jobs indexed by the job id
    bm_job **active_jobs;
    // Current job to be processed (replaces ASIC_jobs_queue)
    bm_job *current_job;
    //semaphone
    SemaphoreHandle_t semaphore;
} AsicTaskModule;

typedef struct
{
    work_queue stratum_queue;

    SystemModule SYSTEM_MODULE;
    DeviceConfig DEVICE_CONFIG;
    DisplayConfig DISPLAY_CONFIG;
    AsicTaskModule ASIC_TASK_MODULE;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    SelfTestModule SELF_TEST_MODULE;
    HashrateMonitorModule HASHRATE_MONITOR_MODULE;

    char * extranonce_str;
    int extranonce_2_len;

    uint8_t * valid_jobs;
    pthread_mutex_t valid_jobs_lock;

    double pool_difficulty;
    bool new_set_mining_difficulty_msg;
    uint32_t version_mask;
    bool new_stratum_version_rolling_msg;

    esp_transport_handle_t transport;
    portMUX_TYPE stratum_mux;
    
    // A message ID that must be unique per request that expects a response.
    // For requests not expecting a response (called notifications), this is null.
    int send_uid;

    stratum_protocol_t stratum_protocol;
    struct sv2_conn *sv2_conn;
    struct sv2_noise_ctx *sv2_noise_ctx;

    bool ASIC_initalized;
    bool psram_is_available;
    bool filesystem_is_available;

    int block_height;
    int coinbase_output_count;
    uint64_t network_nonce_diff;
    uint64_t coinbase_value_total_satoshis;
    uint64_t coinbase_value_user_satoshis;
    coinbase_output_t coinbase_outputs[MAX_COINBASE_TX_OUTPUTS];
    char scriptsig[128];
    char block_signals[MAX_BLOCK_SIGNALS][MAX_BLOCK_SIGNAL_LEN];
    char network_diff_string[DIFF_STRING_SIZE];
    int block_signals_count;
} GlobalState;

#endif /* GLOBAL_STATE_H_ */
