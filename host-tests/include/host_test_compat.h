#ifndef ESP_MINER_HOST_TEST_COMPAT_H
#define ESP_MINER_HOST_TEST_COMPAT_H

#include <stddef.h>

typedef void (*host_test_function_t)(void);

void host_test_register(const char *name, const char *tags,
                        host_test_function_t function,
                        const char *file, int line);
size_t host_strlcpy(char *destination, const char *source, size_t size);

#define HOST_TEST_JOIN_INNER(left, right) left##right
#define HOST_TEST_JOIN(left, right) HOST_TEST_JOIN_INNER(left, right)
#define HOST_TEST_FUNCTION(line) HOST_TEST_JOIN(host_test_function_, line)
#define HOST_TEST_REGISTER(line) HOST_TEST_JOIN(host_test_register_, line)

#define TEST_CASE(name, tags)                                                   \
    static void HOST_TEST_FUNCTION(__LINE__)(void);                            \
    static void __attribute__((constructor)) HOST_TEST_REGISTER(__LINE__)(void)\
    {                                                                          \
        host_test_register((name), (tags), HOST_TEST_FUNCTION(__LINE__),        \
                           __FILE__, __LINE__);                                \
    }                                                                          \
    static void HOST_TEST_FUNCTION(__LINE__)(void)

#define strlcpy host_strlcpy

#endif
