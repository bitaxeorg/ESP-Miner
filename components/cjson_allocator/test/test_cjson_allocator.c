#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "sdkconfig.h"
#include "unity.h"

TEST_CASE("cJSON allocations satisfy alignment and memory placement", "[cjson]")
{
    bool uses_psram = false;
#ifdef CONFIG_SPIRAM
    uses_psram = esp_psram_is_initialized();
#endif
    // Exercise node allocations and strings whose lengths are not aligned.
    const size_t sizes[] = {1, 7, 8, sizeof(cJSON), 65};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        void *ptr = cJSON_malloc(sizes[i]);
        TEST_ASSERT_NOT_NULL(ptr);
        TEST_ASSERT_EQUAL_UINT32(0, (uintptr_t)ptr % _Alignof(cJSON));
        TEST_ASSERT_EQUAL(uses_psram, esp_ptr_external_ram(ptr));
        memset(ptr, 0x5a, sizes[i]);
        cJSON_free(ptr);
    }

    // Rounding a request up to the required alignment must not wrap to zero.
    TEST_ASSERT_NULL(cJSON_malloc(SIZE_MAX));
}

TEST_CASE("cJSON aligned allocator supports parse print and delete", "[cjson]")
{
    const char *json = "{\"value\":42.5,\"label\":\"hello\",\"items\":[true,null,3]}";

    for (int i = 0; i < 8; ++i) {
        cJSON *root = cJSON_Parse(json);
        TEST_ASSERT_NOT_NULL(root);
        TEST_ASSERT_EQUAL_DOUBLE(42.5, cJSON_GetNumberValue(cJSON_GetObjectItem(root, "value")));

        // A small initial buffer exercises cJSON's growth with custom hooks.
        char *printed = cJSON_PrintBuffered(root, 1, false);
        TEST_ASSERT_NOT_NULL(printed);
        cJSON *roundtrip = cJSON_Parse(printed);
        TEST_ASSERT_NOT_NULL(roundtrip);
        TEST_ASSERT_TRUE(cJSON_Compare(root, roundtrip, true));

        cJSON_Delete(roundtrip);
        cJSON_free(printed);
        cJSON_Delete(root);
    }
}
