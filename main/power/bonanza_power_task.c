#include "bonanza_power_task.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include "bzm_board_power.h"
#include "bzm/frequency.h"
#include "bzm/power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"


#define POLL_RATE_MS 100U
#define REQUEST_TIMEOUT_MS 180000U

static const char *TAG = "bonanza_power_management";
static QueueHandle_t requests;
static SemaphoreHandle_t board_io_lock;
static bonanza_power_operations_t board_operations;
static TaskHandle_t owner_task;
static bonanza_power_policy_t policy;
static atomic_bool ready;
static atomic_bool initialized;
static atomic_bool fan_allowed;
static atomic_bool maintenance;
static atomic_bool boot_complete;
static atomic_bool boot_success;
/* Stop intent is monotonic, so a concurrent request cannot be erased by a
 * start finishing or a caller timing out. Only an explicit resume accepts it. */
static atomic_uint stop_epoch;
static atomic_uint accepted_epoch;

typedef enum { BONANZA_POWER_REQUEST_PAUSE, BONANZA_POWER_REQUEST_RESUME,
               BONANZA_POWER_REQUEST_ACQUIRE, BONANZA_POWER_REQUEST_RELEASE } bonanza_power_request_kind_t;

static void power_management_task(void *parameter);

typedef enum { REQUEST_WAITING, REQUEST_COMPLETE, REQUEST_CANCELLED } request_state_t;
typedef struct {
    bonanza_power_request_kind_t kind;
    bonanza_power_owner_t owner;
    unsigned epoch;
    SemaphoreHandle_t completion;
    atomic_uint references;
    atomic_int state;
    bool success;
} bonanza_power_request_t;

static void release_request(bonanza_power_request_t *request)
{
    if (atomic_fetch_sub_explicit(&request->references, 1, memory_order_acq_rel) == 1) {
        vSemaphoreDelete(request->completion);
        free(request);
    }
}

bool BONANZA_POWER_MANAGEMENT_stop_requested(void)
{
    return atomic_load_explicit(&stop_epoch, memory_order_acquire) !=
           atomic_load_explicit(&accepted_epoch, memory_order_acquire);
}

static void revoke_work(void)
{
    atomic_fetch_add_explicit(&stop_epoch, 1, memory_order_acq_rel);
}

static bool request(bonanza_power_request_kind_t kind, bonanza_power_owner_t owner, uint32_t timeout_ms)
{
    if (kind < BONANZA_POWER_REQUEST_PAUSE || kind > BONANZA_POWER_REQUEST_RELEASE ||
        requests == NULL || !atomic_load_explicit(&initialized, memory_order_acquire)) return false;
    if (kind == BONANZA_POWER_REQUEST_PAUSE || kind == BONANZA_POWER_REQUEST_ACQUIRE) revoke_work();
    bonanza_power_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) return false;
    request->completion = xSemaphoreCreateBinary();
    if (request->completion == NULL) { free(request); return false; }
    request->kind = kind;
    request->owner = owner;
    request->epoch = atomic_load_explicit(&stop_epoch, memory_order_acquire);
    atomic_init(&request->references, 2);
    atomic_init(&request->state, REQUEST_WAITING);
    if (xQueueSend(requests, &request, 0) != pdTRUE) {
        release_request(request);
        release_request(request);
        return false;
    }
    if (owner_task != NULL) xTaskNotifyGive(owner_task);
    bool completed = xSemaphoreTake(request->completion, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    if (!completed) {
        int expected = REQUEST_WAITING;
        if (atomic_compare_exchange_strong_explicit(&request->state, &expected,
                REQUEST_CANCELLED, memory_order_acq_rel, memory_order_acquire)) {
            revoke_work();
        } else {
            /* Completion won the timeout race; its result was published
             * before the state transition and is still owned by this caller. */
            completed = expected == REQUEST_COMPLETE;
        }
    }
    bool success = completed && request->success;
    release_request(request);
    return success;
}

bool BONANZA_POWER_MANAGEMENT_pause(void) { return request(BONANZA_POWER_REQUEST_PAUSE, BONANZA_POWER_OWNER_NONE, REQUEST_TIMEOUT_MS); }
bool BONANZA_POWER_MANAGEMENT_resume(void) { return request(BONANZA_POWER_REQUEST_RESUME, BONANZA_POWER_OWNER_NONE, REQUEST_TIMEOUT_MS); }
bool BONANZA_POWER_MANAGEMENT_acquire_maintenance(bonanza_power_owner_t owner) { return request(BONANZA_POWER_REQUEST_ACQUIRE, owner, REQUEST_TIMEOUT_MS); }
bool BONANZA_POWER_MANAGEMENT_release_maintenance(bonanza_power_owner_t owner) { return request(BONANZA_POWER_REQUEST_RELEASE, owner, REQUEST_TIMEOUT_MS); }
bool BONANZA_POWER_MANAGEMENT_prepare_restart(void) { return request(BONANZA_POWER_REQUEST_ACQUIRE, BONANZA_POWER_OWNER_RESTART, REQUEST_TIMEOUT_MS); }
bool BONANZA_POWER_MANAGEMENT_fan_control_allowed(void)
{
    return atomic_load_explicit(&fan_allowed, memory_order_acquire) && !BONANZA_POWER_MANAGEMENT_stop_requested();
}
bool BONANZA_POWER_MANAGEMENT_board_io_begin(void)
{
    if (!atomic_load_explicit(&initialized, memory_order_acquire)) return false;
    if (board_io_lock == NULL || xSemaphoreTake(board_io_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (atomic_load_explicit(&maintenance, memory_order_acquire)) {
        xSemaphoreGive(board_io_lock);
        return false;
    }
    return true;
}
void BONANZA_POWER_MANAGEMENT_board_io_end(void)
{
    xSemaphoreGive(board_io_lock);
}

static bonanza_power_start_result_t board_start(void *context)
{
    atomic_store_explicit(&fan_allowed, false, memory_order_release);
    xSemaphoreTake(board_io_lock, portMAX_DELAY);
    bonanza_power_start_result_t result = board_operations.start(context);
    xSemaphoreGive(board_io_lock);
    return result;
}
static bool board_stop(void *context)
{
    atomic_store_explicit(&fan_allowed, false, memory_order_release);
    xSemaphoreTake(board_io_lock, portMAX_DELAY);
    bool result = board_operations.stop(context);
    xSemaphoreGive(board_io_lock);
    return result;
}
static bool board_maintenance(void *context, bonanza_power_owner_t owner, bool acquire)
{
    atomic_store_explicit(&fan_allowed, false, memory_order_release);
    xSemaphoreTake(board_io_lock, portMAX_DELAY);
    bool result = board_operations.maintenance(context, owner, acquire);
    xSemaphoreGive(board_io_lock);
    return result;
}

esp_err_t BONANZA_POWER_MANAGEMENT_init(GlobalState *state)
{
    if (state == NULL || requests != NULL) return ESP_ERR_INVALID_STATE;
    requests = xQueueCreate(4, sizeof(bonanza_power_request_t *));
    if (requests == NULL) return ESP_ERR_NO_MEM;
    board_io_lock = xSemaphoreCreateMutex();
    if (board_io_lock == NULL) {
        vQueueDelete(requests);
        requests = NULL;
        return ESP_ERR_NO_MEM;
    }
    /* Above result dispatch (15), below the independent bridge service (18). */
    if (xTaskCreate(power_management_task, "power management", 8192,
                    state, 16, &owner_task) != pdPASS) {
        vQueueDelete(requests);
        vSemaphoreDelete(board_io_lock);
        board_io_lock = NULL;
        requests = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void BONANZA_POWER_MANAGEMENT_set_ready(void)
{
    atomic_store_explicit(&ready, true, memory_order_release);
    if (owner_task != NULL) xTaskNotifyGive(owner_task);
}

bool BONANZA_POWER_MANAGEMENT_wait_started(uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (!atomic_load_explicit(&boot_complete, memory_order_acquire)) {
        if (esp_timer_get_time() >= deadline) { revoke_work(); return false; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return atomic_load_explicit(&boot_success, memory_order_acquire);
}

static void process_request(bonanza_power_request_t *request)
{
    bool cancelled = atomic_load_explicit(&request->state, memory_order_acquire) == REQUEST_CANCELLED;
    bool success = false;
    if (request->kind == BONANZA_POWER_REQUEST_PAUSE) {
        success = bonanza_power_policy_pause(&policy);
    } else if (!cancelled || request->kind == BONANZA_POWER_REQUEST_RELEASE) {
        /* A release must drain even after its caller times out, otherwise an
         * update that has already finished would retain board ownership. */
        switch (request->kind) {
        case BONANZA_POWER_REQUEST_RESUME:
            if (request->epoch == atomic_load_explicit(&stop_epoch, memory_order_acquire)) {
                atomic_store_explicit(&accepted_epoch, request->epoch, memory_order_release);
                success = bonanza_power_policy_resume(&policy);
            }
            break;
        case BONANZA_POWER_REQUEST_ACQUIRE:
            atomic_store_explicit(&maintenance, true, memory_order_release);
            success = bonanza_power_policy_maintenance(&policy, request->owner, true);
            break;
        case BONANZA_POWER_REQUEST_RELEASE:
            success = bonanza_power_policy_maintenance(&policy, request->owner, false);
            if (success) atomic_store_explicit(&maintenance, false, memory_order_release);
            break;
        default: break;
        }
    }
    request->success = success;
    int expected = REQUEST_WAITING;
    if (!atomic_compare_exchange_strong_explicit(&request->state, &expected,
            REQUEST_COMPLETE, memory_order_acq_rel, memory_order_acquire)) {
        if (success && request->kind == BONANZA_POWER_REQUEST_ACQUIRE)
            (void)bonanza_power_policy_maintenance(&policy, request->owner, false);
        if (policy.owner == BONANZA_POWER_OWNER_NONE) (void)bonanza_power_policy_pause(&policy);
    }
    atomic_store_explicit(&maintenance, policy.owner != BONANZA_POWER_OWNER_NONE, memory_order_release);
    xSemaphoreGive(request->completion);
    release_request(request);
}

static void set_overheat(void *context, bool enabled)
{
    GlobalState *state = context;
    if (enabled) {
        nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, false);
        nvs_config_set_u16(NVS_CONFIG_MANUAL_FAN_SPEED, 100);
    }
    nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, enabled);
    state->SYSTEM_MODULE.overheat_mode = enabled;
}

static bool save_target(void *context, bonanza_power_target_t target)
{
    GlobalState *state = context;
    if ((state->DEVICE_CONFIG.family.id == BONANZA)) {
        bzm_frequency_target_t resolved;
        float voltage;
        if (!bzm_frequency_resolve_target(target.frequency_mhz, &resolved) ||
            !bzm_power_resolve_user_voltage(target.voltage_mv, &voltage)) return false;
        target.frequency_mhz = resolved.actual_mhz;
    }
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, target.voltage_mv);
    nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, target.frequency_mhz);
    return nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE) == target.voltage_mv &&
           fabsf(nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY) - target.frequency_mhz) < 0.001f;
}

static bool cancelled(void *context)
{
    GlobalState *state = context;
    return BONANZA_POWER_MANAGEMENT_stop_requested() || state->SYSTEM_MODULE.pools_unavailable;
}

static esp_err_t bonanza_operations_init(GlobalState *state, bonanza_power_operations_t *ops)
{
    *ops = (bonanza_power_operations_t){
        .context = state, .start = BZM_board_start, .stop = BZM_board_stop,
        .apply = BZM_board_apply, .maintenance = BZM_board_maintenance,
        .overheat = set_overheat, .save_target = save_target, .cancelled = cancelled,
        .minimum_voltage_mv = 2100, .minimum_frequency_mhz = BZM_FREQUENCY_TARGET_MIN_MHZ,
    };
    return BZM_board_init(state);
}
static void power_management_task(void *parameter)
{
    GlobalState *state = parameter;
    xSemaphoreTake(board_io_lock, portMAX_DELAY);
    esp_err_t board_result = bonanza_operations_init(state, &board_operations);
    xSemaphoreGive(board_io_lock);
    bonanza_power_operations_t operations = board_operations;
    operations.start = board_start;
    operations.stop = board_stop;
    operations.maintenance = board_maintenance;
    if (!bonanza_power_policy_init(&policy, operations)) {
        ESP_LOGE(TAG, "Invalid board power operations");
        atomic_store(&boot_complete, true);
        vTaskDelete(NULL);
        return;
    }
    policy.fault = board_result != ESP_OK;
    atomic_store_explicit(&initialized, true, memory_order_release);
    for (;;) {
        policy.ready = atomic_load_explicit(&ready, memory_order_acquire);
        bonanza_power_target_t target = {
            .voltage_mv = state->SELF_TEST_MODULE.is_active
                ? state->DEVICE_CONFIG.family.asic.default_voltage_mv
                : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE),
            .frequency_mhz = state->SELF_TEST_MODULE.is_active
                ? state->DEVICE_CONFIG.family.asic.default_frequency_mhz
                : nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY),
        };
        policy.target = target;
        policy.pool_unavailable = state->SYSTEM_MODULE.pools_unavailable;
        bonanza_power_request_t *request;
        while (xQueueReceive(requests, &request, 0) == pdTRUE) process_request(request);
        /* A failed enqueue or timed-out caller still revokes mining. */
        if (BONANZA_POWER_MANAGEMENT_stop_requested() && !policy.paused && policy.owner == BONANZA_POWER_OWNER_NONE)
            (void)bonanza_power_policy_pause(&policy);
        /* No board access during firmware maintenance. */
        bonanza_power_sample_t sample = policy.owner == BONANZA_POWER_OWNER_NONE
            ? BZM_board_sample() : (bonanza_power_sample_t){0};
        if (state->SELF_TEST_MODULE.is_finished) policy.paused = true;
        bonanza_power_policy_step(&policy, (uint64_t)(esp_timer_get_time() / 1000), target, sample,
            state->SYSTEM_MODULE.pools_unavailable, state->SYSTEM_MODULE.hardware_fault,
            nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE));
        state->SYSTEM_MODULE.mining_paused = policy.paused;
        atomic_store_explicit(&fan_allowed, policy.running && !policy.fault && !policy.cooling, memory_order_release);
        if (policy.ready && (policy.running || policy.fault || policy.paused || policy.cooling)) {
            atomic_store_explicit(&boot_success, policy.running, memory_order_release);
            atomic_store_explicit(&boot_complete, true, memory_order_release);
        }
        if (policy.fault) {
            state->POWER_MANAGEMENT_MODULE.expected_hashrate = 0;
            if (!state->SYSTEM_MODULE.hardware_fault ||
                state->SYSTEM_MODULE.hardware_fault_msg[0] == '\0') {
                snprintf(state->SYSTEM_MODULE.hardware_fault_msg,
                    sizeof(state->SYSTEM_MODULE.hardware_fault_msg), "%.*s",
                    (int)sizeof(state->SYSTEM_MODULE.hardware_fault_msg) - 1,
                    sample.detail[0] ? sample.detail : "Board startup, shutdown or power transition failed");
            }
            state->SYSTEM_MODULE.hardware_fault = true;
        }
        /* Notifications only shorten this wait; hardware ownership never
         * moves to an HTTP, Stratum, fan, or bridge-update caller. */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_RATE_MS));
    }
}
