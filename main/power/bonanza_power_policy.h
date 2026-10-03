#ifndef BONANZA_POWER_POLICY_H
#define BONANZA_POWER_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { uint16_t voltage_mv; float frequency_mhz; } bonanza_power_target_t;
typedef enum { BONANZA_POWER_HEALTH_OK, BONANZA_POWER_HEALTH_OVERHEAT, BONANZA_POWER_HEALTH_FAULT } bonanza_power_health_t;
typedef enum { BONANZA_POWER_START_OK, BONANZA_POWER_START_FAILED, BONANZA_POWER_START_OVERHEAT } bonanza_power_start_result_t;
typedef enum { BONANZA_POWER_OWNER_NONE, BONANZA_POWER_OWNER_OTA, BONANZA_POWER_OWNER_RESTART } bonanza_power_owner_t;
typedef struct {
    bonanza_power_health_t health;
    bool vreg_valid;
    float vreg_c;
    char detail[160];
} bonanza_power_sample_t;
typedef struct {
    void *context;
    bonanza_power_start_result_t (*start)(void *context);
    /* Success means the board-specific shutdown contract was verified. */
    bool (*stop)(void *context);
    /* One bounded tuning step; waiting for work replacement is success. */
    bool (*apply)(void *context, bonanza_power_target_t target);
    bool (*maintenance)(void *context, bonanza_power_owner_t owner, bool acquire);
    void (*overheat)(void *context, bool enabled);
    bool (*save_target)(void *context, bonanza_power_target_t target);
    bool (*cancelled)(void *context);
    uint16_t minimum_voltage_mv;
    float minimum_frequency_mhz;
} bonanza_power_operations_t;
typedef struct {
    bonanza_power_operations_t operations;
    bool ready;
    bool running;
    bool stopped;
    bool paused;
    bool fault;
    bool cooling;
    bool reduced_target_saved;
    bool pool_unavailable;
    bonanza_power_target_t target;
    bonanza_power_owner_t owner;
    uint64_t cooling_since_ms;
    bonanza_power_target_t cooling_target;
} bonanza_power_policy_t;

bool bonanza_power_policy_init(bonanza_power_policy_t *policy, bonanza_power_operations_t operations);
/* Only BONANZA_POWER_MANAGEMENT_task calls these functions and hardware operations. */
bool bonanza_power_policy_pause(bonanza_power_policy_t *policy);
bool bonanza_power_policy_resume(bonanza_power_policy_t *policy);
bool bonanza_power_policy_maintenance(bonanza_power_policy_t *policy, bonanza_power_owner_t owner, bool acquire);
void bonanza_power_policy_step(bonanza_power_policy_t *policy, uint64_t now_ms,
                       bonanza_power_target_t target, bonanza_power_sample_t sample,
                       bool pools_unavailable, bool hardware_fault,
                       bool persisted_overheat);

#endif
