#include "unity.h"
#include "stratum_api.h"
#include "esp_heap_caps.h"
#include "esp_transport.h"

// A custom-cert TLS pool with no certificate makes STRATUM_V1_transport_init
// fail. The SSL transport it already allocated must be freed, otherwise every
// reconnect attempt leaks it.
TEST_CASE("Custom cert transport init without a cert does not leak", "[stratum]")
{
    // First call can allocate one-time SDK state, so measure after it.
    TEST_ASSERT_NULL(STRATUM_V1_transport_init(CUSTOM_CRT, NULL));

    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_NULL(STRATUM_V1_transport_init(CUSTOM_CRT, NULL));
    }
    size_t after = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    TEST_ASSERT_EQUAL_UINT32(before, after);
}
