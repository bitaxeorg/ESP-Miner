#include "bzm_test_state.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BZM_init adapter_test_init
#define BZM_set_frequency adapter_test_set_frequency
#define BZM_set_difficulty adapter_test_set_difficulty
#define BZM_driver_state_init adapter_test_state_init
#define BZM_set_max_baud adapter_test_set_max_baud
#define BZM_submit_job adapter_test_submit_job
#define BZM_clear_work adapter_test_clear_work
#define BZM_job_frequency_ms adapter_test_job_frequency_ms
#define BZM_process_work adapter_test_process_work
#define BZM_read_temperature adapter_test_read_temperature
#define BZM_hashrate_counter_snapshot adapter_test_hashrate_snapshot
#define BZM_has_io_fault adapter_test_has_io_fault
#define BZM_work_replacement_snapshot adapter_test_replacement_snapshot
#define BZM_get_telemetry_snapshot adapter_test_telemetry_snapshot
#define BZM_step_frequency_domains adapter_test_step_frequency_domains
#define BZM_start adapter_test_start
#define BZM_get_state adapter_test_get_state
#define BZM_hold_reset adapter_test_hold_reset
#define BZM_set_dispatch_authorizer adapter_test_dispatch_authorizer
#define BZM_set_operation_authorizer adapter_test_operation_authorizer
#define BZM_poll adapter_test_poll

#define bzm_platform_esp_init adapter_mock_platform_init
#define bzm_driver_state_init adapter_mock_state_init
#define bzm_driver_init adapter_mock_init
#define bzm_driver_set_difficulty adapter_mock_set_difficulty
#define bzm_driver_hold_reset adapter_mock_hold_reset
#define bzm_driver_process_result adapter_mock_process_result
#define create_jobs_refresh adapter_mock_refresh
static BaseType_t adapter_create_task(TaskFunction_t task, const char *name, void *context, TaskHandle_t *handle);
#define xTaskCreate(task, name, stack, context, priority, handle) adapter_create_task(task, name, context, handle)
#include "../../bzm/port/esp/driver.c"

static bool platform_ok, allocation_ok, task_ok, difficulty_ok;
static uint8_t applied_difficulty;
static uint8_t startup_count;
static unsigned task_calls, reset_calls;
static bzm_device_t last_device;
static bzm_result_t share;
static GlobalState *refresh_state;

void adapter_mock_refresh(GlobalState *state) { refresh_state = state; }

bool adapter_mock_platform_init(void) { return platform_ok; }
bool adapter_mock_state_init(bzm_device_t *device) { last_device = *device; return allocation_ok; }
uint8_t adapter_mock_init(bzm_device_t *device)
{
    bzm_device_record_frequency(device, 800);
    device->difficulty_power = 0;
    return startup_count;
}
bool adapter_mock_set_difficulty(bzm_device_t *device, uint8_t power)
{
    last_device = *device;
    applied_difficulty = power;
    device->difficulty_power = power;
    return difficulty_ok;
}
bool adapter_mock_hold_reset(void) { ++reset_calls; return true; }
const bzm_result_t *adapter_mock_process_result(bzm_device_t *device) { (void)device; return &share; }
static BaseType_t adapter_create_task(TaskFunction_t task, const char *name, void *context, TaskHandle_t *handle)
{
    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_EQUAL_STRING("bzm io", name);
    TEST_ASSERT_EQUAL_PTR(last_device.context, context);
    ++task_calls;
    if (!task_ok) return pdFAIL;
    *handle = (TaskHandle_t)(uintptr_t)1;
    return pdPASS;
}

TEST_CASE("BZM ESP adapter owns task retry, application callbacks and result conversion", "[asic][bzm][adapter]")
{
    GlobalState state = {0};
    state.DEVICE_CONFIG.family.asic.id = BZM;
    state.DEVICE_CONFIG.family.asic.core_count = 236;
    state.DEVICE_CONFIG.family.asic_count = 4;
    state.ASIC_initalized = true;
    TEST_ASSERT_EQUAL_UINT8(0, BZM_init(NULL));
    platform_ok = false;
    TEST_ASSERT_EQUAL_UINT8(0, BZM_init(&state));
    platform_ok = true;
    allocation_ok = false;
    TEST_ASSERT_EQUAL_UINT8(0, BZM_init(&state));
    allocation_ok = true;
    startup_count = 0;
    TEST_ASSERT_EQUAL_UINT8(0, BZM_init(&state));
    TEST_ASSERT_EQUAL_UINT32(0, task_calls);
    startup_count = 4;
    task_ok = false;
    TEST_ASSERT_EQUAL_UINT8(0, BZM_init(&state));
    TEST_ASSERT_EQUAL_UINT32(1, reset_calls);
    task_ok = true;
    TEST_ASSERT_EQUAL_UINT8(4, BZM_init(&state));
    TEST_ASSERT_EQUAL_UINT8(4, BZM_init(&state));
    TEST_ASSERT_EQUAL_UINT32(2, task_calls);
    TEST_ASSERT_EQUAL_FLOAT(800, state.POWER_MANAGEMENT_MODULE.actual_frequency);
    TEST_ASSERT_EQUAL_UINT8(0, state.current_difficulty_power);
    difficulty_ok = true;
    BZM_set_difficulty(&state, 9);
    TEST_ASSERT_EQUAL_UINT8(9, applied_difficulty);
    TEST_ASSERT_EQUAL_PTR(&state, last_device.context);
    TEST_ASSERT_FALSE(state.SYSTEM_MODULE.hardware_fault);
    TEST_ASSERT_TRUE(last_device.can_submit(last_device.context));
    TEST_ASSERT_NOT_NULL(last_device.request_work);
    last_device.request_work(last_device.context);
    TEST_ASSERT_EQUAL_PTR(&state, refresh_state);
    state.SYSTEM_MODULE.pools_unavailable = true;
    TEST_ASSERT_FALSE(last_device.can_submit(last_device.context));
    state.SYSTEM_MODULE.pools_unavailable = false;
    last_device.report_fault(last_device.context, "fixture fault");
    TEST_ASSERT_TRUE(state.SYSTEM_MODULE.hardware_fault);
    TEST_ASSERT_EQUAL_STRING("fixture fault", state.SYSTEM_MODULE.hardware_fault_msg);
    TEST_ASSERT_FALSE(last_device.can_submit(last_device.context));
    share = (bzm_result_t){
        .job_valid = true, .job = {.ntime = 100, .version = 0x20000000},
        .final_ntime = 101, .final_version = 0x20002000, .nonce = 42,
    };
    task_result *result = BZM_process_work(&state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_UINT32(101, result->job.ntime);
    TEST_ASSERT_EQUAL_UINT32(100, share.job.ntime);
    TEST_ASSERT_EQUAL_HEX32(0x20002000, result->rolled_version);
    state.SYSTEM_MODULE.hardware_fault = false;
    difficulty_ok = false;
    BZM_set_difficulty(&state, 10);
    TEST_ASSERT_TRUE(state.SYSTEM_MODULE.hardware_fault);
    TEST_ASSERT_EQUAL_STRING("BZM difficulty update failed", state.SYSTEM_MODULE.hardware_fault_msg);
    share.job_valid = false;
    TEST_ASSERT_NULL(BZM_process_work(&state));
}
