#include "unity.h"

#include "bm13xx_fixture.h"
#include "stratum_api.h"
#include "sv2_protocol.h"
#include "utils.h"

#include <string.h>

typedef struct {
    char bytes[256];
    size_t length;
} sv1_capture_t;

static int capture_sv1_write(esp_transport_handle_t transport,
                             const char *buffer, int length, int timeout_ms)
{
    (void)timeout_ms;
    sv1_capture_t *capture =
        (sv1_capture_t *)esp_transport_get_context_data(transport);
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_GREATER_THAN_INT(0, length);
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(capture->bytes), (uint32_t)length);
    memcpy(capture->bytes, buffer, (size_t)length);
    capture->bytes[length] = '\0';
    capture->length = (size_t)length;
    return length;
}

static esp_transport_handle_t create_sv1_capture(sv1_capture_t *capture)
{
    esp_transport_handle_t transport = esp_transport_init();
    if (transport == NULL ||
        esp_transport_set_context_data(transport, capture) != ESP_OK ||
        esp_transport_set_func(transport, NULL, NULL, capture_sv1_write,
                               NULL, NULL, NULL, NULL) != ESP_OK) {
        esp_transport_destroy(transport);
        return NULL;
    }
    return transport;
}

/* Fixed wire values, including CRC5. Expected bytes are not built by the
 * production encoder or by a second copy of the version-mask algorithm. */
static const uint8_t full_mask_command[] = {
    0x55, 0xaa, 0x51, 0x09, 0x00, 0xa4, 0x90, 0x00, 0xff, 0xff, 0x1c,
};

static void assert_packet(const uint8_t expected[11], size_t index)
{
    const bm13xx_fixture_packet_t *packet = bm13xx_fixture_packet(index);
    TEST_ASSERT_EQUAL_UINT(11, packet->length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet->bytes, 11);
}

static void queue_job_response(const bm13xx_fixture_driver_t *driver,
                               uint8_t version_high, uint8_t version_low)
{
    /* receive_work supplies an already-framed response. Its CRC validation is
     * outside this fixture; the last byte marks a job rather than a register. */
    const uint8_t response[] = {
        0xaa, 0x55, 0x9b, 0x04, 0x4c, 0x0a, 0x00,
        driver->response_job_id, version_high, version_low, 0x80,
    };
    bm13xx_fixture_queue_response(response);
}

TEST_CASE("BM13xx version mask register command remains byte exact",
          "[asic][version-rolling][characterization]")
{
    static const struct {
        uint32_t mask;
        uint8_t expected[11];
    } cases[] = {
        {0,          {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 0, 0, 0x17}},
        {0x00002000, {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 0, 1, 0x12}},
        {0x00200000, {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 1, 0, 0x0b}},
        {0x02468000, {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 0x12, 0x34, 6}},
        {0x1fffe000, {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 0xff, 0xff, 0x1c}},
        /* Bits outside the hardware's 16-bit rolling field are discarded. */
        {0xe0001fff, {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 0, 0, 0x17}},
        {0xffffffff, {0x55, 0xaa, 0x51, 9, 0, 0xa4, 0x90, 0, 0xff, 0xff, 0x1c}},
    };

    for (size_t d = 0; d < BM13XX_FIXTURE_DRIVER_COUNT; d++) {
        bm13xx_fixture_begin();
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            bm13xx_fixture_clear_packets();
            bm13xx_fixture_drivers[d].set_version_mask(cases[c].mask);
            TEST_ASSERT_EQUAL_UINT(1, bm13xx_fixture_packet_count());
            assert_packet(cases[c].expected, 0);
        }
        bm13xx_fixture_end();
    }
}

TEST_CASE("BM13xx initialization enables the full version rolling mask",
          "[asic][version-rolling][characterization]")
{
    for (size_t d = 0; d < BM13XX_FIXTURE_DRIVER_COUNT; d++) {
        const bm13xx_fixture_driver_t *driver = &bm13xx_fixture_drivers[d];
        GlobalState *state = bm13xx_fixture_begin();
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, driver->init(state), driver->name);
        unsigned mask_count = 0;
        for (size_t p = 0; p < bm13xx_fixture_packet_count(); p++) {
            const bm13xx_fixture_packet_t *packet = bm13xx_fixture_packet(p);
            if (packet->length == 11 && packet->bytes[5] == 0xa4) {
                assert_packet(full_mask_command, p);
                mask_count++;
            }
        }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(driver->init_mask_count, mask_count, driver->name);
        bm13xx_fixture_end();
    }
}

TEST_CASE("BM13xx response version bits reconstruct rolled block versions",
          "[asic][version-rolling][characterization]")
{
    static const struct {
        uint32_t base_version;
        uint8_t wire_bytes[2];
        uint32_t expected_version;
    } cases[] = {
        {0x20000004, {0x00, 0x00}, 0x20000004},
        {0x20000004, {0x00, 0x01}, 0x20002004},
        {0x20000004, {0x00, 0x03}, 0x20006004},
        {0x20000004, {0x01, 0x00}, 0x20200004},
        {0x20000004, {0x80, 0x00}, 0x30000004},
        {0x20000004, {0xff, 0xff}, 0x3fffe004},
        /* The current drivers OR returned bits with the saved base version. */
        {0x21002004, {0x00, 0x01}, 0x21002004},
        {0x21002004, {0x00, 0x02}, 0x21006004},
    };

    for (size_t d = 0; d < BM13XX_FIXTURE_DRIVER_COUNT; d++) {
        const bm13xx_fixture_driver_t *driver = &bm13xx_fixture_drivers[d];
        GlobalState *state = bm13xx_fixture_begin();
        TEST_ASSERT_EQUAL_UINT8(2, driver->init(state));
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            bm13xx_fixture_set_job(cases[c].base_version, true, true);
            queue_job_response(driver, cases[c].wire_bytes[0], cases[c].wire_bytes[1]);
            const task_result *result = driver->process_work(state);
            TEST_ASSERT_NOT_NULL(result);
            TEST_ASSERT_EQUAL_HEX32(cases[c].expected_version, result->rolled_version);
            TEST_ASSERT_EQUAL_HEX8(0x10, result->job_id);
            TEST_ASSERT_EQUAL_HEX32(0x0a4c049b, result->nonce);
            TEST_ASSERT_EQUAL_UINT8(1, result->asic_nr);
            TEST_ASSERT_EQUAL_UINT8(77, result->core_id);
            TEST_ASSERT_EQUAL_UINT8(3, result->small_core_id);
            /* ESP-IDF's Unity build does not enable the UINT64 assertions. */
            TEST_ASSERT_TRUE(result->timestamp_us == UINT64_C(123456789));
            TEST_ASSERT_EQUAL_INT(REGISTER_INVALID, result->register_type);
            TEST_ASSERT_NULL(driver->process_work(state)); /* response consumed */
        }
        bm13xx_fixture_end();
    }
}

TEST_CASE("BM13xx missing jobs and register replies do not produce rolled shares",
          "[asic][version-rolling][characterization]")
{
    for (size_t d = 0; d < BM13XX_FIXTURE_DRIVER_COUNT; d++) {
        const bm13xx_fixture_driver_t *driver = &bm13xx_fixture_drivers[d];
        GlobalState *state = bm13xx_fixture_begin();
        TEST_ASSERT_EQUAL_UINT8(2, driver->init(state));
        TEST_ASSERT_NULL(driver->process_work(state));
        for (unsigned flags = 0; flags < 3; flags++) {
            bm13xx_fixture_set_job(0x20000004, (flags & 1) != 0, (flags & 2) != 0);
            queue_job_response(driver, 0xff, 0xff);
            TEST_ASSERT_NULL(driver->process_work(state));
        }
        bm13xx_fixture_set_job(0x20000004, true, true);
        queue_job_response(driver, 0, 1);
        TEST_ASSERT_NOT_NULL(driver->process_work(state));

        /* A register result must clear the previous job's version and nonce. */
        const uint8_t register_response[] = {
            0xaa, 0x55, 0x12, 0x34, 0x56, 0x78, 0, 0x4c, 0, 0, 0,
        };
        bm13xx_fixture_queue_response(register_response);
        const task_result *result = driver->process_work(state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(REGISTER_ERROR_COUNT, result->register_type);
        TEST_ASSERT_EQUAL_HEX32(0x12345678, result->value);
        TEST_ASSERT_EQUAL_HEX32(0, result->rolled_version);
        TEST_ASSERT_EQUAL_HEX32(0, result->nonce);
        const uint8_t unknown_register[] = {
            0xaa, 0x55, 0, 0, 0, 0, 0, 1, 0, 0, 0,
        };
        bm13xx_fixture_queue_response(unknown_register);
        TEST_ASSERT_NULL(driver->process_work(state));
        bm13xx_fixture_end();
    }
}

TEST_CASE("BM1373 version mask writes retry without changing command bytes",
          "[asic][version-rolling][characterization]")
{
    const bm13xx_fixture_driver_t *driver = &bm13xx_fixture_drivers[3];
    bm13xx_fixture_begin();
    for (unsigned failures = 0; failures <= 3; failures++) {
        bm13xx_fixture_clear_packets();
        bm13xx_fixture_fail_writes(failures);
        driver->set_version_mask(BIP320_VERSION_ROLLING_MASK);
        unsigned attempts = failures < 3 ? failures + 1 : 3;
        TEST_ASSERT_EQUAL_UINT(attempts, bm13xx_fixture_packet_count());
        TEST_ASSERT_EQUAL_UINT(attempts - 1, bm13xx_fixture_delay_count());
        for (unsigned p = 0; p < attempts; p++) {
            assert_packet(full_mask_command, p);
        }
    }
    bm13xx_fixture_end();
}

TEST_CASE("BM13xx rolled version reaches SV1 and SV2 share messages byte exact",
          "[asic][version-rolling][stratum][characterization]")
{
    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());
    for (size_t d = 0; d < BM13XX_FIXTURE_DRIVER_COUNT; d++) {
        const bm13xx_fixture_driver_t *driver = &bm13xx_fixture_drivers[d];
        GlobalState *state = bm13xx_fixture_begin();
        TEST_ASSERT_EQUAL_UINT8(2, driver->init(state));
        const uint32_t base_version = 0x20000004;
        bm13xx_fixture_set_job(base_version, true, true);
        queue_job_response(driver, 0, 1);
        const task_result *result = driver->process_work(state);
        TEST_ASSERT_NOT_NULL(result);
        const uint32_t rolled_version = result->rolled_version;
        const uint32_t version_bits = rolled_version ^ base_version;
        TEST_ASSERT_EQUAL_HEX32(0x20002004, rolled_version);
        TEST_ASSERT_EQUAL_HEX32(0x00002000, version_bits);

        /* Capture the bytes at the transport seam without a network peer. */
        sv1_capture_t sv1_capture = {0};
        esp_transport_handle_t sv1_transport = create_sv1_capture(&sv1_capture);
        TEST_ASSERT_NOT_NULL(sv1_transport);
        int sv1_length = STRATUM_V1_submit_share(
            sv1_transport, 7, "worker", "job-1", "0000000000000000",
            0x64658bd8, result->nonce, version_bits, NULL);
        const char *expected_sv1 =
            "{\"id\":7,\"method\":\"mining.submit\",\"params\":["
            "\"worker\",\"job-1\",\"0000000000000000\","
            "\"64658bd8\",\"0a4c049b\",\"00002000\"]}\n";
        TEST_ASSERT_EQUAL_INT((int)strlen(expected_sv1), sv1_length);
        TEST_ASSERT_EQUAL_UINT((size_t)sv1_length, sv1_capture.length);
        TEST_ASSERT_EQUAL_STRING(expected_sv1, sv1_capture.bytes);
        esp_transport_destroy(sv1_transport);

        uint8_t sv2_message[SV2_SUBMIT_SHARES_MAX_FRAME_SIZE] = {0};
        int sv2_length = sv2_build_submit_shares(
            sv2_message, sizeof(sv2_message), 0x11223344, 0x55667788,
            0x90abcdef, result->nonce, 0x64658bd8, rolled_version, NULL, 0);
        const uint8_t expected_sv2[] = {
            0x00, 0x80, SV2_MSG_SUBMIT_SHARES_STANDARD, 0x18, 0x00, 0x00,
            0x44, 0x33, 0x22, 0x11,
            0x88, 0x77, 0x66, 0x55,
            0xef, 0xcd, 0xab, 0x90,
            0x9b, 0x04, 0x4c, 0x0a,
            0xd8, 0x8b, 0x65, 0x64,
            0x04, 0x20, 0x00, 0x20,
        };
        TEST_ASSERT_EQUAL_INT(sizeof(expected_sv2), sv2_length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_sv2, sv2_message, sizeof(expected_sv2));
        bm13xx_fixture_end();
    }
}
