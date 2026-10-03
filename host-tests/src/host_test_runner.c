#include "host_test_compat.h"
#include "unity.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_TEST_CAPACITY 256

typedef struct {
    const char *name;
    const char *tags;
    host_test_function_t function;
    const char *file;
    int line;
} host_test_t;

static host_test_t tests[HOST_TEST_CAPACITY];
static size_t test_count;

void host_test_register(const char *name, const char *tags,
                        host_test_function_t function,
                        const char *file, int line)
{
    if (test_count >= HOST_TEST_CAPACITY) {
        fprintf(stderr, "host test capacity exceeded\n");
        abort();
    }

    tests[test_count++] = (host_test_t) {
        .name = name,
        .tags = tags,
        .function = function,
        .file = file,
        .line = line,
    };
}

size_t host_strlcpy(char *destination, const char *source, size_t size)
{
    size_t source_length = strlen(source);

    if (size > 0) {
        size_t copy_length = source_length < size - 1 ? source_length : size - 1;
        memcpy(destination, source, copy_length);
        destination[copy_length] = '\0';
    }

    return source_length;
}

__attribute__((weak)) void setUp(void)
{
}

__attribute__((weak)) void tearDown(void)
{
}

static bool test_matches(const host_test_t *test, const char *filter)
{
    return filter == NULL || strstr(test->name, filter) != NULL ||
           strstr(test->tags, filter) != NULL;
}

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : NULL;
    size_t selected = 0;

    UNITY_BEGIN();
    for (size_t index = 0; index < test_count; ++index) {
        if (!test_matches(&tests[index], filter)) {
            continue;
        }

        ++selected;
        Unity.TestFile = tests[index].file;
        UnityDefaultTestRun(tests[index].function, tests[index].name,
                            tests[index].line);
    }

    if (selected == 0) {
        fprintf(stderr, "no host tests matched '%s'\n", filter ? filter : "");
        return EXIT_FAILURE;
    }

    return UNITY_END();
}
