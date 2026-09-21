#include "unity.h"

#include "bzm/lease_guard_internal.h"

typedef struct
{
    uint8_t renew_count;
    uint8_t fail_renew_at;
    uint8_t sleep_count;
    uint32_t sleeps[8];
} lease_mock_t;

static bool mock_renew(void * context)
{
    lease_mock_t * mock = context;
    ++mock->renew_count;
    return mock->fail_renew_at == 0 || mock->renew_count != mock->fail_renew_at;
}

static void mock_sleep(void * context, uint32_t delay_ms)
{
    lease_mock_t * mock = context;
    TEST_ASSERT_LESS_THAN_UINT8(sizeof(mock->sleeps) / sizeof(mock->sleeps[0]), mock->sleep_count);
    mock->sleeps[mock->sleep_count++] = delay_ms;
}

static bzm_bridge_safety_status_t controlled_status(void)
{
    return (bzm_bridge_safety_status_t){
        .valid = true,
        .state = BZM_BRIDGE_SAFETY_STATE_CONTROLLED,
        .fault = BZM_BRIDGE_SAFETY_FAULT_NONE,
        .runtime_verdict = BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED,
        .evidence =
            BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID | BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR,
        .lease_remaining_ms = 2000,
    };
}

TEST_CASE("BZM startup lease status requires live controlled evidence", "[asic][bzm][lease_guard]")
{
    bzm_bridge_safety_status_t status = controlled_status();
    TEST_ASSERT_TRUE(bzm_lease_guard_status_is_controlled(&status));

    status.lease_remaining_ms = 0;
    TEST_ASSERT_FALSE(bzm_lease_guard_status_is_controlled(&status));
    status = controlled_status();
    status.valid = false;
    TEST_ASSERT_FALSE(bzm_lease_guard_status_is_controlled(&status));
    status = controlled_status();
    status.state = BZM_BRIDGE_SAFETY_STATE_SAFE_OFF;
    TEST_ASSERT_FALSE(bzm_lease_guard_status_is_controlled(&status));
    status = controlled_status();
    status.trip_input_asserted = true;
    TEST_ASSERT_FALSE(bzm_lease_guard_status_is_controlled(&status));
}

TEST_CASE("BZM startup lease delay renews at intervals no longer than 250 ms", "[asic][bzm][lease_guard]")
{
    lease_mock_t mock = {0};
    TEST_ASSERT_TRUE(bzm_lease_guard_delay(760, mock_renew, mock_sleep, &mock));
    TEST_ASSERT_EQUAL_UINT8(4, mock.renew_count);
    TEST_ASSERT_EQUAL_UINT8(4, mock.sleep_count);
    TEST_ASSERT_EQUAL_UINT32(250, mock.sleeps[0]);
    TEST_ASSERT_EQUAL_UINT32(250, mock.sleeps[1]);
    TEST_ASSERT_EQUAL_UINT32(250, mock.sleeps[2]);
    TEST_ASSERT_EQUAL_UINT32(10, mock.sleeps[3]);
}

TEST_CASE("BZM startup lease delay stops before a failed-renewal chunk", "[asic][bzm][lease_guard]")
{
    lease_mock_t mock = {.fail_renew_at = 3};
    TEST_ASSERT_FALSE(bzm_lease_guard_delay(1000, mock_renew, mock_sleep, &mock));
    TEST_ASSERT_EQUAL_UINT8(3, mock.renew_count);
    TEST_ASSERT_EQUAL_UINT8(2, mock.sleep_count);
    TEST_ASSERT_EQUAL_UINT32(250, mock.sleeps[0]);
    TEST_ASSERT_EQUAL_UINT32(250, mock.sleeps[1]);
}

TEST_CASE("BZM lease schedule services long work at a bounded interval", "[asic][bzm][lease_guard][schedule]")
{
    lease_mock_t mock = {0};
    bzm_lease_guard_schedule_t schedule = {0};

    TEST_ASSERT_TRUE(bzm_lease_guard_service_due(&schedule, 1000, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(1, mock.renew_count);
    TEST_ASSERT_TRUE(bzm_lease_guard_service_due(&schedule, 1249, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(1, mock.renew_count);
    TEST_ASSERT_TRUE(bzm_lease_guard_service_due(&schedule, 1250, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(2, mock.renew_count);
    TEST_ASSERT_TRUE(bzm_lease_guard_service_due(&schedule, 1501, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(3, mock.renew_count);
}

TEST_CASE("BZM lease schedule renews on clock regression and preserves failed state", "[asic][bzm][lease_guard][schedule]")
{
    lease_mock_t mock = {0};
    bzm_lease_guard_schedule_t schedule = {0};

    TEST_ASSERT_TRUE(bzm_lease_guard_service_due(&schedule, 500, 250, mock_renew, &mock));
    TEST_ASSERT_TRUE(bzm_lease_guard_service_due(&schedule, 100, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(2, mock.renew_count);
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t) schedule.last_renewal_ms);

    mock.fail_renew_at = 3;
    TEST_ASSERT_FALSE(bzm_lease_guard_service_due(&schedule, 350, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(3, mock.renew_count);
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t) schedule.last_renewal_ms);
    TEST_ASSERT_FALSE(bzm_lease_guard_service_due(NULL, 500, 250, mock_renew, &mock));
    TEST_ASSERT_FALSE(bzm_lease_guard_service_due(&schedule, 500, 0, mock_renew, &mock));
    TEST_ASSERT_FALSE(bzm_lease_guard_service_due(&schedule, 500, 250, NULL, &mock));
}

TEST_CASE("BZM lease schedule uses the live operation authorization", "[asic][bzm][lease_guard][schedule]")
{
    lease_mock_t mock = {0};
    bzm_lease_guard_schedule_t schedule = {0};

    TEST_ASSERT_FALSE(bzm_lease_guard_service_authorized(&schedule, false, 1000, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(0, mock.renew_count);
    TEST_ASSERT_FALSE(schedule.renewed);

    TEST_ASSERT_TRUE(bzm_lease_guard_service_authorized(&schedule, true, 1000, 250, mock_renew, &mock));
    TEST_ASSERT_EQUAL_UINT8(1, mock.renew_count);
    TEST_ASSERT_TRUE(schedule.renewed);
}

TEST_CASE("BZM execution deadline is strict and zero is fail closed", "[asic][bzm][lease_guard]")
{
    TEST_ASSERT_FALSE(bzm_lease_guard_deadline_allows(0, 0));
    TEST_ASSERT_TRUE(bzm_lease_guard_deadline_allows(11000, 10999));
    TEST_ASSERT_FALSE(bzm_lease_guard_deadline_allows(11000, 11000));
    TEST_ASSERT_FALSE(bzm_lease_guard_deadline_allows(11000, 11001));
}

TEST_CASE("BZM execution deadline construction rejects zero and wraparound", "[asic][bzm][lease_guard]")
{
    uint64_t deadline = 123;
    TEST_ASSERT_TRUE(bzm_lease_guard_make_deadline(1000, 10000, &deadline));
    TEST_ASSERT_EQUAL_UINT32(11000, (uint32_t) deadline);
    TEST_ASSERT_FALSE(bzm_lease_guard_make_deadline(1000, 0, &deadline));
    TEST_ASSERT_FALSE(bzm_lease_guard_make_deadline(UINT64_MAX - 5, 10, &deadline));
    TEST_ASSERT_FALSE(bzm_lease_guard_make_deadline(1000, 10, NULL));
}
