#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "power_management_task.h"
#include "asic_job_slot.h"
#include "hashrate_monitor_task.h"
#include "coinbase_decoder.h"
#include "device_config.h"
#include "display.h"
#include "scoreboard.h"
#include "esp_transport.h"
#include "system.h"

typedef struct PoolConfig
{
    char * url;
    uint16_t port;
    char * user;
    char * pass;
    stratum_protocol_t protocol;
    uint16_t difficulty;
    bool extranonce_subscribe;
    uint16_t tls;
    char * cert;
    bool decode_coinbase_tx;
    bool share_warning;
    uint16_t sv2_channel_type;
    char * sv2_authority_pubkey;
    bool sv2_require_auth;
} PoolConfig;

#define DIFF_STRING_SIZE 10
#define MAX_BLOCK_SIGNALS 8
#define MAX_BLOCK_SIGNAL_LEN 16
#define MAX_POOLS 8

typedef struct RejectedReasonStat
{
    char message[64];
    uint32_t count;
} RejectedReasonStat;

typedef struct {
    const esp_partition_t *part;
    char version[32];
    char compileDate[16];
    char compileTime[16];
    int usagePercent;
    bool isCurrent;
} cached_partition_t;

typedef struct SystemModule
{
    float current_hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float error_percentage;
    int64_t start_time_us;
    uint64_t shares_submitted;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint16_t shares_pending;
    uint64_t work_received;
    RejectedReasonStat rejected_reason_stats[10];
    int rejected_reason_stats_count;
    int screen_page;
    uint64_t best_nonce_diff;
    char best_diff_string[DIFF_STRING_SIZE];
    uint64_t best_session_nonce_diff;
    char best_session_diff_string[DIFF_STRING_SIZE];
    int block_found;
    bool show_new_block;
    char * ssid;
    char wifi_status[256];
    char ip_addr_str[16]; // IP4ADDR_STRLEN_MAX
    char ipv6_addr_str[64]; // IPv6 address string with zone identifier (INET6_ADDRSTRLEN=46 + % + interface=15)
    char ap_ssid[12];
    bool ap_enabled;
    bool is_connected;
    int identify_mode_time_ms;
    PoolConfig pools[MAX_POOLS];
    uint16_t primary_pool_index;
    uint16_t secondary_pool_index;
    bool use_fallback_stratum;
    bool is_using_fallback;
    float response_time;
    float response_time_min;
    float response_time_max;
    float last_response_time_min;
    float last_response_time_max;
    uint16_t response_share_batch;
    float process_time;
    float cpu_usage;
    double pool_difficulty;
    char pool_connection_info[64];
    char primary_pool_error[64];
    char pool_banner[128];
    bool overheat_mode;
    bool mining_paused;
    bool pools_unavailable;
    uint16_t power_fault;
    uint32_t lastClockSync;
    bool is_screen_active;
    bool is_firmware_update;
    char firmware_update_filename[20];
    char firmware_update_status[20];
    bool hardware_fault;
    char hardware_fault_msg[64];
    const char * asic_status;
    char * version;
    char * axeOSVersion;
    Scoreboard scoreboard;
    uint64_t uptime_seconds;
    cached_partition_t cached_partitions[3];
    int cached_partitions_count;
    char mdns_hostname[64];
    char full_hostname[70];
} SystemModule;

typedef struct SelfTestNonceMeasurement
{
    bool is_active;
    uint8_t target[32];
    uint64_t accepted_count;
    uint64_t rejected_count;
    double hashes;
    pthread_mutex_t lock;
} SelfTestNonceMeasurement;

typedef struct SelfTestModule
{
    bool is_active;
    bool is_factory;
    bool is_finished;
    SelfTestNonceMeasurement nonce_measurement;
    const char *message;
    char *result;
    char *finished;
    esp_err_t system_init_ret;
} SelfTestModule;

typedef struct GlobalState
{
    TaskHandle_t create_jobs_task_handle;
    volatile uint8_t active_job_slot_idx;

    SystemModule SYSTEM_MODULE;
    DeviceConfig DEVICE_CONFIG;
    DisplayConfig DISPLAY_CONFIG;
    asic_job_store_t job_store;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    SelfTestModule SELF_TEST_MODULE;
    HashrateMonitorModule HASHRATE_MONITOR_MODULE;

    uint8_t current_difficulty_power;
    double target_share_interval_s;
    esp_transport_handle_t transport;
    pthread_mutex_t transport_mutex;

    bool ASIC_initalized;
    uint32_t ASIC_work_epoch; // Increment after successful ASIC initialization.
    bool psram_is_available;
    bool filesystem_is_available;

    int block_height;
    mining_notification_result_t coinbase;
    mining_notification_result_t coinbase_staging;
    uint64_t network_nonce_diff;
    char network_diff_string[DIFF_STRING_SIZE];
    char block_signals[MAX_BLOCK_SIGNALS][MAX_BLOCK_SIGNAL_LEN];
    int block_signals_count;
} GlobalState;

#endif /* GLOBAL_STATE_H_ */
