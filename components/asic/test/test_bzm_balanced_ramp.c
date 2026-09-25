#include <string.h>

#include "bzm/balanced_ramp.h"
#include "bzm/registers.h"
#include "unity.h"

typedef struct
{
    int fail_begin_at;
    int fail_write_at;
    int fail_read_at;
    size_t begin_count;
    size_t write_count;
    size_t read_count;
    size_t read_call_count;
    size_t delay_count;
    uint32_t total_delay_ms;
    uint16_t begun_engines[4];
    uint16_t write_engines[4];
    uint8_t write_offsets[4];
    size_t write_lengths[4];
    uint32_t write_values[4];
    float ch0_mv;
    float ch1_mv;
    bool telemetry_valid;
    bool fail_all_reads;
    uint8_t status;
    size_t busy_after_reads;
    uint8_t config;
} ramp_mock_t;

static bool mock_begin(void * context, uint8_t asic_id, uint16_t engine_id)
{
    ramp_mock_t * mock = context;
    TEST_ASSERT_TRUE(bzm_topology_asic_index(asic_id, NULL));
    if (mock->fail_begin_at >= 0 && mock->begin_count == (size_t) mock->fail_begin_at) {
        return false;
    }
    if (mock->begin_count < sizeof(mock->begun_engines) / sizeof(mock->begun_engines[0])) {
        mock->begun_engines[mock->begin_count] = engine_id;
    }
    ++mock->begin_count;
    return true;
}

static bool mock_write(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, const void * data,
                       size_t data_len)
{
    ramp_mock_t * mock = context;
    (void) asic_id;
    (void) engine_id;
    (void) offset;
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN_UINT32(0, data_len);
    if (mock->fail_write_at >= 0 && mock->write_count == (size_t) mock->fail_write_at) {
        return false;
    }
    if (mock->write_count < 4) {
        mock->write_engines[mock->write_count] = engine_id;
        mock->write_offsets[mock->write_count] = offset;
        mock->write_lengths[mock->write_count] = data_len;
        memcpy(&mock->write_values[mock->write_count], data, data_len < sizeof(uint32_t) ? data_len : sizeof(uint32_t));
    }
    ++mock->write_count;
    return true;
}

static bool mock_read(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data, size_t data_len)
{
    ramp_mock_t * mock = context;
    (void) asic_id;
    (void) engine_id;
    TEST_ASSERT_EQUAL_HEX8(BZM_ENGINE_REG_STATUS, offset);
    TEST_ASSERT_EQUAL_UINT32(4, data_len);
    size_t call = mock->read_call_count++;
    if (mock->fail_all_reads || (mock->fail_read_at >= 0 && call == (size_t) mock->fail_read_at)) {
        return false;
    }
    uint8_t * bytes = data;
    memset(bytes, 0, data_len);
    bytes[0] = mock->read_count >= mock->busy_after_reads ? mock->status : 0;
    bytes[1] = mock->config;
    ++mock->read_count;
    return true;
}

static void mock_delay(void * context, uint32_t delay_ms)
{
    ramp_mock_t * mock = context;
    ++mock->delay_count;
    mock->total_delay_ms += delay_ms;
}

static bool mock_telemetry(void * context, uint8_t asic_id, bzm_telemetry_sample_t * sample)
{
    ramp_mock_t * mock = context;
    (void) asic_id;
    *sample = (bzm_telemetry_sample_t){
        .received = true,
        .valid = mock->telemetry_valid,
        .ch0_mv = mock->ch0_mv,
        .ch1_mv = mock->ch1_mv,
    };
    return true;
}

static const bzm_balanced_ramp_ops_t MOCK_OPS = {
    .begin_engine = mock_begin,
    .write_register = mock_write,
    .read_register = mock_read,
    .delay_ms = mock_delay,
    .telemetry_sample = mock_telemetry,
};

static ramp_mock_t good_mock(void)
{
    return (ramp_mock_t){
        .fail_begin_at = -1,
        .fail_write_at = -1,
        .fail_read_at = -1,
        .ch0_mv = 360.0f,
        .ch1_mv = 340.0f,
        .telemetry_valid = true,
        .status = BZM_BALANCED_RAMP_ENGINE_BUSY_MASK,
        .busy_after_reads = 0,
        .config = BZM_BALANCED_RAMP_ENGINE_CONFIG | BZM_BALANCED_RAMP_ENGINE_CONFIG_ACTIVE_MASK,
    };
}

TEST_CASE("BZM engine activation activates the higher-voltage stack first", "[asic][bzm][activation]")
{
    ramp_mock_t mock = good_mock();
    bzm_balanced_ramp_report_t ramp;
    bzm_engine_pair_t pair;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(0, &pair));

    TEST_ASSERT_TRUE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &mock, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL_UINT16(BZM_CONTROL_ENGINE_ID, mock.begun_engines[0]);
    TEST_ASSERT_EQUAL_UINT16(pair.bottom.physical_id, mock.begun_engines[1]);
    TEST_ASSERT_EQUAL_UINT16(pair.top.physical_id, mock.begun_engines[2]);
    TEST_ASSERT_EQUAL_UINT32(48, mock.write_count);
    TEST_ASSERT_EQUAL_UINT32(2, mock.read_count);
    TEST_ASSERT_EQUAL_UINT32(2, mock.delay_count);
    TEST_ASSERT_EQUAL_UINT32(2, mock.total_delay_ms);
    TEST_ASSERT_EQUAL_UINT16(BZM_CONTROL_ENGINE_ID, mock.write_engines[0]);
    TEST_ASSERT_EQUAL_UINT16(BZM_CONTROL_ENGINE_ID, mock.write_engines[1]);
    TEST_ASSERT_EQUAL_HEX8(BZM_LOCAL_REG_ENGINE_SOFT_RESET, mock.write_offsets[0]);
    TEST_ASSERT_EQUAL_HEX8(BZM_LOCAL_REG_ENGINE_SOFT_RESET, mock.write_offsets[1]);
    TEST_ASSERT_EQUAL_UINT32(4, mock.write_lengths[0]);
    TEST_ASSERT_EQUAL_UINT32(4, mock.write_lengths[1]);
    TEST_ASSERT_EQUAL_HEX32(0, mock.write_values[0]);
    TEST_ASSERT_EQUAL_HEX32(1, mock.write_values[1]);

    ramp_mock_t top_first = good_mock();
    top_first.ch0_mv = 330.0f;
    top_first.ch1_mv = 355.0f;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &top_first, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL_UINT16(pair.top.physical_id, top_first.begun_engines[1]);
    TEST_ASSERT_EQUAL_UINT16(pair.bottom.physical_id, top_first.begun_engines[2]);
}

TEST_CASE("BZM engine activation fails closed when engine-domain reset cannot complete", "[asic][bzm][activation]")
{
    ramp_mock_t mock = good_mock();
    mock.fail_write_at = 1;
    bzm_balanced_ramp_report_t ramp;
    bzm_engine_pair_t pair;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(0, &pair));

    TEST_ASSERT_FALSE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &mock, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL(BZM_BALANCED_RAMP_FAILURE_ENGINE_RESET, ramp.failure);
}

TEST_CASE("BZM engine activation fails closed with at most one engine of pair skew", "[asic][bzm][activation]")
{
    ramp_mock_t mock = good_mock();
    mock.fail_begin_at = 2;
    bzm_balanced_ramp_report_t ramp;
    bzm_engine_pair_t pair;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(0, &pair));

    TEST_ASSERT_FALSE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &mock, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL(BZM_BALANCED_RAMP_FAILURE_LEASE, ramp.failure);
    TEST_ASSERT_EQUAL_UINT32(1, mock.read_count);
    TEST_ASSERT_EQUAL_UINT32(25, mock.write_count);
    TEST_ASSERT_EQUAL_UINT32(2, mock.begin_count);
}

TEST_CASE("BZM engine activation requires busy and enhanced config acknowledgement", "[asic][bzm][activation]")
{
    bzm_engine_pair_t pair;
    TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(0, &pair));

    ramp_mock_t idle = good_mock();
    idle.status = 0;
    bzm_balanced_ramp_report_t ramp;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_FALSE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &idle, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL(BZM_BALANCED_RAMP_FAILURE_NOT_BUSY, ramp.failure);
    TEST_ASSERT_EQUAL_UINT32(3, idle.read_count);

    ramp_mock_t gated = good_mock();
    gated.config = 0;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_FALSE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &gated, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL(BZM_BALANCED_RAMP_FAILURE_CONFIG_READBACK, ramp.failure);

    ramp_mock_t undocumented = good_mock();
    undocumented.config |= 0x08;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_FALSE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &undocumented, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL(BZM_BALANCED_RAMP_FAILURE_CONFIG_READBACK, ramp.failure);

    ramp_mock_t inactive_marker = good_mock();
    inactive_marker.config = BZM_BALANCED_RAMP_ENGINE_CONFIG;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &inactive_marker, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
}

TEST_CASE("BZM engine activation accepts a bounded delayed busy acknowledgement", "[asic][bzm][activation]")
{
    bzm_engine_pair_t pair;
    TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(0, &pair));

    ramp_mock_t mock = good_mock();
    mock.busy_after_reads = 1;
    bzm_balanced_ramp_report_t ramp;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &mock, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL_UINT32(3, mock.read_count);
    TEST_ASSERT_EQUAL_UINT32(3, mock.delay_count);
    TEST_ASSERT_EQUAL_UINT32(3, mock.total_delay_ms);
}

TEST_CASE("BZM engine activation retries a transient status timeout but rejects a continuous timeout", "[asic][bzm][activation]")
{
    bzm_engine_pair_t pair;
    TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(0, &pair));

    ramp_mock_t transient = good_mock();
    transient.fail_read_at = 0;
    bzm_balanced_ramp_report_t ramp;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_TRUE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &transient, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL_UINT32(3, transient.read_call_count);
    TEST_ASSERT_EQUAL_UINT32(2, transient.read_count);

    ramp_mock_t continuous = good_mock();
    continuous.fail_all_reads = true;
    ramp = (bzm_balanced_ramp_report_t){0};
    TEST_ASSERT_FALSE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &continuous, BZM_BRINGUP_FIRST_ASIC_ID, &pair));
    TEST_ASSERT_EQUAL(BZM_BALANCED_RAMP_FAILURE_STATUS_READ, ramp.failure);
    TEST_ASSERT_EQUAL_UINT32(3, continuous.read_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, continuous.read_count);
}

TEST_CASE("BZM engine activation acknowledges all 944 engines and resets each ASIC once", "[asic][bzm][activation]")
{
    ramp_mock_t mock = good_mock();
    bzm_balanced_ramp_report_t ramp;
    ramp = (bzm_balanced_ramp_report_t){0};

    for (uint16_t pair_index = 0; pair_index < BZM_TOPOLOGY_PAIR_COUNT; ++pair_index) {
        bzm_engine_pair_t pair;
        TEST_ASSERT_TRUE(bzm_topology_balanced_pair_at(pair_index, &pair));
        for (uint8_t asic_index = 0; asic_index < BZM_BRINGUP_ASIC_COUNT; ++asic_index) {
            TEST_ASSERT_TRUE(bzm_balanced_ramp_commit_pair(&ramp, &MOCK_OPS, &mock,
                                                           bzm_asic_wire_ids[asic_index], &pair));
        }
    }
    TEST_ASSERT_EQUAL_UINT32(944, mock.read_count);
    TEST_ASSERT_EQUAL_UINT32(948, mock.begin_count);
    TEST_ASSERT_EQUAL_UINT32(21720, mock.write_count);
    TEST_ASSERT_EQUAL_UINT32(8, mock.delay_count);
    TEST_ASSERT_EQUAL_UINT32(8, mock.total_delay_ms);
}
