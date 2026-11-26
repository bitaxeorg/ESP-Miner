#include "check.h"
#include "bzm/job_store.h"
#include "bzm/posix.h"
#include "bzm/reactor.h"
#include <string.h>

static asic_job_t job_template(void)
{
    asic_job_t job = {
        .version = 0x20000000,
        .version_mask = 0x6000,
        .ntime = 0x65010203,
        .nbits = 0x1705dd01,
        .starting_nonce = 0x12345678,
        .pool_id = 2,
        .source_type = JOB_TYPE_SV2_STANDARD,
        .job_id = "retained-job",
        .extranonce2 = "abcd",
    };
    memset(job.prev_hash, 0x12, sizeof(job.prev_hash));
    memset(job.merkle_root, 0x34, sizeof(job.merkle_root));
    memset(job.pool_target, 0x56, sizeof(job.pool_target));
    return job;
}

static void check_snapshot(bzm_job_store_t *store, bzm_work_handle_t handle,
                           const asic_job_t *expected)
{
    asic_job_t snapshot;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(bzm_job_store_snapshot(store, handle, &snapshot));
    CHECK(snapshot.version == expected->version && snapshot.version_mask == expected->version_mask);
    CHECK(snapshot.ntime == expected->ntime && snapshot.nbits == expected->nbits);
    CHECK(snapshot.starting_nonce == expected->starting_nonce);
    CHECK(snapshot.pool_id == expected->pool_id && snapshot.source_type == expected->source_type);
    CHECK(memcmp(snapshot.prev_hash, expected->prev_hash, sizeof(snapshot.prev_hash)) == 0);
    CHECK(memcmp(snapshot.merkle_root, expected->merkle_root, sizeof(snapshot.merkle_root)) == 0);
    CHECK(memcmp(snapshot.pool_target, expected->pool_target, sizeof(snapshot.pool_target)) == 0);
    CHECK(memcmp(snapshot.job_id, expected->job_id, sizeof(snapshot.job_id)) == 0);
    CHECK(memcmp(snapshot.extranonce2, expected->extranonce2, sizeof(snapshot.extranonce2)) == 0);
}

static void check_missing_snapshot(bzm_job_store_t *store, bzm_work_handle_t handle)
{
    asic_job_t snapshot;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(!bzm_job_store_snapshot(store, handle, &snapshot));
    const unsigned char cleared[sizeof(snapshot)] = {0};
    CHECK(memcmp(&snapshot, cleared, sizeof(snapshot)) == 0);
}

static void test_store(void)
{
    bzm_job_store_t store;
    CHECK(bzm_job_store_init(&store));
    CHECK(store.capacity == 256);
    asic_job_t job = job_template(), snapshot;
    unsigned char untouched[sizeof(snapshot)];
    memset(untouched, 0xa5, sizeof(untouched));
    memcpy(&snapshot, untouched, sizeof(snapshot));
    bzm_job_store_t empty = {0};
    CHECK(!bzm_job_store_snapshot(NULL, 0, &snapshot));
    CHECK(!bzm_job_store_snapshot(&empty, 0, &snapshot));
    CHECK(!bzm_job_store_snapshot(&store, BZM_WORK_HANDLE_INVALID, &snapshot));
    store.capacity = 0;
    CHECK(!bzm_job_store_snapshot(&store, 0, &snapshot));
    store.capacity = BZM_JOB_STORE_CAPACITY;
    CHECK(memcmp(&snapshot, untouched, sizeof(snapshot)) == 0);
    bzm_work_handle_t handles[BZM_JOB_STORE_CAPACITY];
    for (size_t i = 0; i < BZM_JOB_STORE_CAPACITY; ++i) {
        check_missing_snapshot(&store, i);
        job.ntime = (uint32_t)i;
        CHECK(bzm_job_store_store_generated(&store, &job, &handles[i]));
        CHECK((handles[i] & 0xff) == i);
        check_snapshot(&store, handles[i], &job);
    }
    bzm_work_handle_t replacement;
    job.ntime = 999;
    CHECK(bzm_job_store_store_generated(&store, &job, &replacement));
    CHECK((replacement & 0xff) == 0 && replacement != handles[0]);
    CHECK(!bzm_job_store_contains(&store, handles[0]));
    check_missing_snapshot(&store, handles[0]);
    CHECK(!bzm_job_store_release(&store, handles[0]));
    check_snapshot(&store, replacement, &job);
    job.ntime = 1;
    check_snapshot(&store, handles[1], &job);
    CHECK(bzm_job_store_release(&store, handles[1]));
    CHECK(!bzm_job_store_release(&store, handles[1]));
    check_missing_snapshot(&store, handles[1]);

    bzm_job_store_invalidate_all(&store);
    for (size_t i = 0; i < BZM_JOB_STORE_CAPACITY; ++i) {
        CHECK(!bzm_job_store_contains(&store, handles[i]));
        check_missing_snapshot(&store, handles[i]);
    }
    CHECK(!bzm_job_store_contains(&store, replacement));
    bzm_work_handle_t restarted;
    CHECK(bzm_job_store_store_generated(&store, &job, &restarted));
    CHECK((restarted & 0xff) == 0 && restarted != replacement && restarted != handles[0]);
    check_snapshot(&store, restarted, &job);
    CHECK(bzm_job_store_store_generated(&store, &job, NULL));
    bzm_job_store_destroy(&store);
}

typedef struct {
    size_t count;
    bzm_work_t last;
} transport_t;

static bool write_work(void *context, const bzm_work_t *work)
{
    transport_t *transport = context;
    ++transport->count;
    transport->last = *work;
    return true;
}

static void test_reactor_bounds(void)
{
    bzm_job_store_t store;
    CHECK(bzm_job_store_init(&store));
    bzm_reactor_t *reactor = calloc(1, sizeof(*reactor));
    CHECK(reactor != NULL);
    CHECK(sizeof(bzm_assignment_t) <= 56);
    CHECK(sizeof(reactor->assignments) / sizeof(reactor->assignments[0]) == 236);
    CHECK(sizeof(reactor->previous_assignments) / sizeof(reactor->previous_assignments[0]) == 236);
    CHECK(sizeof(reactor->next_engine_sequence) == 236);
    bzm_reactor_config_t config = {.engine_count = 237, .timestamp_count = 16, .lead_zeros = 36};
    const bzm_transport_ops_t ops = {.write_work = write_work};
    transport_t transport = {0};
    CHECK(!bzm_reactor_init(reactor, &store, &config, &ops, &transport));
    config.engine_count = 0;
    CHECK(!bzm_reactor_init(reactor, &store, &config, &ops, &transport));
    config.engine_count = 236;
    CHECK(bzm_reactor_init(reactor, &store, &config, &ops, &transport));
    asic_job_t job = job_template();
    for (size_t i = 0; i < config.engine_count; ++i) {
        CHECK(bzm_reactor_assign(reactor, &job, NULL) == BZM_ASSIGN_OK);
        CHECK(reactor->assignments[i].active);
    }
    CHECK(transport.count == 236 && reactor->next_engine == 0);
    bzm_raw_result_t raw = {
        .asic_id = BZM_FIRST_ASIC_ID,
        .engine_id = transport.last.engine_id,
        .sequence_id = transport.last.logical_sequence << 2,
        .status = 8,
        .time = config.timestamp_count,
    };
    bzm_result_t result;
    CHECK(bzm_reactor_map_result(reactor, &raw, &result));
    CHECK(result.work_handle == transport.last.handle && result.final_ntime == job.ntime);
    CHECK(bzm_reactor_assign(reactor, &job, NULL) == BZM_ASSIGN_OK);
    CHECK(reactor->next_engine == 1 && reactor->previous_assignments[0].active);
    CHECK(bzm_reactor_begin_flush(reactor));
    CHECK(!bzm_reactor_map_result(reactor, &raw, &result));
    bzm_reactor_finish_flush(reactor);
    CHECK(!reactor->assignments[235].active && !reactor->previous_assignments[0].active);
    free(reactor);
    bzm_job_store_destroy(&store);
}

int main(void)
{
    bzm_platform_ops_t platform;
    bzm_platform_posix_defaults(&platform);
    CHECK(bzm_platform_bind(&platform, NULL));
    test_store();
    test_reactor_bounds();
    return 0;
}
