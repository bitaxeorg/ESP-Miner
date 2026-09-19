#include <string.h>

#include "bzm.h"
#include "unity.h"

TEST_CASE("BZM consecutive midstate groups do not repeat a header", "[asic][bzm][work]")
{
    asic_job_t job = {.version = 0x20000004, .version_mask = 0x1fffe000};
    bzm_work_ref_t source = {.handle = 1, .template = &job};
    uint32_t versions[8];
    uint8_t midstates[8][32];
    for (size_t group = 0; group < 2; ++group) {
        bzm_work_t work;
        TEST_ASSERT_TRUE(bzm_work_build(&source, 0, 0, 60, 32, &work));

        for (size_t i = 0; i < 4; ++i) {
            size_t index = group * 4 + i;
            versions[index] = work.versions[i];
            memcpy(midstates[index], work.midstates[i], 32);
            TEST_ASSERT_EQUAL_HEX32(0x20000004, work.versions[i] & ~job.version_mask);
            for (size_t previous = 0; previous < index; ++previous) {
                TEST_ASSERT_NOT_EQUAL(versions[previous], versions[index]);
                TEST_ASSERT_NOT_EQUAL(0, memcmp(midstates[previous], midstates[index], 32));
            }
        }
        job.version = 0x20008004; // Producer advances by four negotiated versions.
    }
}

TEST_CASE("BZM midstate versions wrap within a sparse negotiated mask", "[asic][bzm][work]")
{
    asic_job_t job = {.version = 0x3000a004, .version_mask = 0x1000a000};
    bzm_work_ref_t source = {.handle = 1, .template = &job};
    bzm_work_t work;
    TEST_ASSERT_TRUE(bzm_work_build(&source, 0, 0, 60, 32, &work));
    const uint32_t expected[] = {0x3000a004, 0x20000004, 0x20002004, 0x20008004};
    TEST_ASSERT_EQUAL_HEX32_ARRAY(expected, work.versions, 4);
}

TEST_CASE("BZM disabled rolling preserves enhanced FIFO identity", "[asic][bzm][work]")
{
    asic_job_t job = {.version = 0x20000004, .version_mask = 0};
    bzm_work_ref_t source = {.handle = 1, .template = &job};
    bzm_work_t work;
    TEST_ASSERT_TRUE(bzm_work_build(&source, 0, 0, 60, 32, &work));

    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_HEX32(job.version, work.versions[i]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(work.midstates[0], work.midstates[i], 32);
    }
}

TEST_CASE("BZM refuses a timestamp window that would wrap", "[asic][bzm][work]")
{
    asic_job_t job = {.ntime = UINT32_MAX - 59};
    bzm_work_ref_t source = {.handle = 1, .template = &job};
    bzm_work_t work;
    TEST_ASSERT_FALSE(bzm_work_build(&source, 0, 0, 60, 40, &work));
    job.ntime--;
    TEST_ASSERT_TRUE(bzm_work_build(&source, 0, 0, 60, 40, &work));
}
