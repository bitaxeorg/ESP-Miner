#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "asic_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "power_management_task.h"
#include "hashrate_monitor_task.h"
#include "serial.h"
#include "stratum_api.h"
#include "coinbase_decoder.h"
#include "work_queue.h"
#include "device_config.h"
#include "display.h"
#include "esp_transport.h"

#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10
#define MAX_BLOCK_SIGNALS 8
#define MAX_BLOCK_SIGNAL_LEN 16

typedef struct {
    char message[64];
    uint32_t count;
} RejectedReasonStat;

typedef struct
{
    float current_hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float error_percentage;

    bool pool_extranonce_subscribe;
    bool pool_decode_coinbase_tx;
    uint16_t pool_difficulty;
    uint16_t pool_tls;
    uint16_t pool_port;
    char * pool_url;
    char * pool_user;
    char * pool_pass;
    char * pool_cert;

    bool fallback_pool_extranonce_subscribe;
    bool fallback_pool_decode_coinbase_tx;
    uint16_t fallback_pool_difficulty;
    uint16_t fallback_pool_tls;
    uint16_t fallback_pool_port;
    char * fallback_pool_url;
    char * fallback_pool_user;
    char * fallback_pool_pass;
    char * fallback_pool_cert;

    float response_time;
    uint32_t lastClockSync;
    int identify_mode_time_ms;
    int screen_page;
    bool ap_enabled;
    bool is_connected;
    bool is_firmware_update;
    bool is_screen_active;

    bool mining_paused;
    bool overheat_mode;
    bool power_fault;
    bool use_fallback_stratum;
    bool is_using_fallback;
    bool show_new_block;
    int block_found;

    int64_t start_time;
    uint64_t work_received;
    uint32_t shares_accepted;
    uint32_t shares_rejected;

    int rejected_reason_stats_count;
    RejectedReasonStat rejected_reason_stats[10];

    uint64_t best_nonce_diff;
    char best_diff_string[DIFF_STRING_SIZE];
    uint64_t best_session_nonce_diff;
    char best_session_diff_string[DIFF_STRING_SIZE];

    char * ssid;
    char ap_ssid[12];
    char wifi_status[256];
    char ip_addr_str[16]; // IP4ADDR_STRLEN_MAX
    char ipv6_addr_str[64]; // IPv6 address string with zone identifier (INET6_ADDRSTRLEN=46 + % + interface=15)

    char pool_connection_info[64];
    char firmware_update_filename[20];
    char firmware_update_status[20];
    char * asic_status;
    char * version;
    char * axeOSVersion;
} SystemModule;

typedef struct
{
    bool is_active;
    bool is_finished;
    char *message;
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

    uint32_t pool_difficulty;
    bool new_set_mining_difficulty_msg;
    uint32_t version_mask;
    bool new_stratum_version_rolling_msg;

    esp_transport_handle_t transport;
    
    // A message ID that must be unique per request that expects a response.
    // For requests not expecting a response (called notifications), this is null.
    int send_uid;

    bool ASIC_initalized;
    bool psram_is_available;
    bool filesystem_is_available;

    int block_height;
    char scriptsig[128];
    int coinbase_output_count;
    coinbase_output_t coinbase_outputs[MAX_COINBASE_TX_OUTPUTS];
    uint64_t coinbase_value_total_satoshis;
    uint64_t coinbase_value_user_satoshis;
    uint64_t network_nonce_diff;
    char network_diff_string[DIFF_STRING_SIZE];
    int block_signals_count;
    char block_signals[MAX_BLOCK_SIGNALS][MAX_BLOCK_SIGNAL_LEN];
} GlobalState;

#endif /* GLOBAL_STATE_H_ */
