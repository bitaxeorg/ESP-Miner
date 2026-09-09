#include "unity.h"
#include "prometheus_format.h"
#include <string.h>

/* ------------------------------------------------------------------ *
 * prometheus_escape_label_value
 * ------------------------------------------------------------------ */

TEST_CASE("escape: plain ASCII passes through unchanged", "[prometheus_format]")
{
    char dst[64];
    prometheus_escape_label_value("hello", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

TEST_CASE("escape: double-quote is backslash-escaped", "[prometheus_format]")
{
    char dst[64];
    prometheus_escape_label_value("a\"b", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("a\\\"b", dst);
}

TEST_CASE("escape: backslash is doubled", "[prometheus_format]")
{
    char dst[64];
    prometheus_escape_label_value("a\\b", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("a\\\\b", dst);
}

TEST_CASE("escape: newline becomes backslash-n", "[prometheus_format]")
{
    char dst[64];
    prometheus_escape_label_value("a\nb", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("a\\nb", dst);
}

/* ------------------------------------------------------------------ *
 * prometheus_format_labels — no labels
 * ------------------------------------------------------------------ */

TEST_CASE("format_labels: count 0 produces empty string", "[prometheus_format]")
{
    char buf[64];
    prometheus_format_labels(buf, sizeof(buf), NULL, NULL, 0);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* ------------------------------------------------------------------ *
 * prometheus_format_labels — single label
 * ------------------------------------------------------------------ */

TEST_CASE("format_labels: single label produces {key=\"value\"}", "[prometheus_format]")
{
    char buf[64];
    const char *keys[]   = {"fan"};
    const char *values[] = {"1"};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 1);
    TEST_ASSERT_EQUAL_STRING("{fan=\"1\"}", buf);
}

/* Board 601 / Gamma fan RPM label */
TEST_CASE("format_labels: board-601 fan=1 label", "[prometheus_format]")
{
    char buf[64];
    const char *keys[]   = {"fan"};
    const char *values[] = {"1"};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 1);
    /* Full metric line would be: espminer_fan_rpm{fan="1"} 4032 */
    TEST_ASSERT_EQUAL_STRING("{fan=\"1\"}", buf);
    /* Verify no leading metric-name garbage — no '=' before '{' */
    TEST_ASSERT_EQUAL('{', buf[0]);
}

/* Board 601 / Gamma chip temp label */
TEST_CASE("format_labels: board-601 chip=1 label", "[prometheus_format]")
{
    char buf[64];
    const char *keys[]   = {"chip"};
    const char *values[] = {"1"};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 1);
    /* Full metric line would be: espminer_chip_temp_celsius{chip="1"} 53.5 */
    TEST_ASSERT_EQUAL_STRING("{chip=\"1\"}", buf);
    TEST_ASSERT_EQUAL('{', buf[0]);
}

/* ------------------------------------------------------------------ *
 * prometheus_format_labels — multiple labels
 * ------------------------------------------------------------------ */

TEST_CASE("format_labels: two labels produces {k1=\"v1\",k2=\"v2\"}", "[prometheus_format]")
{
    char buf[128];
    const char *keys[]   = {"reason", "pool"};
    const char *values[] = {"low_diff", "primary"};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 2);
    TEST_ASSERT_EQUAL_STRING("{reason=\"low_diff\",pool=\"primary\"}", buf);
}

TEST_CASE("format_labels: value with special chars is escaped", "[prometheus_format]")
{
    char buf[128];
    const char *keys[]   = {"msg"};
    const char *values[] = {"it's a \"test\""};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 1);
    TEST_ASSERT_EQUAL_STRING("{msg=\"it's a \\\"test\\\"\"}", buf);
}

/* ------------------------------------------------------------------ *
 * Regression: label string must start with '{', not bare key="value"
 * (guards against reintroducing the prometheus_format_label bug)
 * ------------------------------------------------------------------ */

TEST_CASE("format_labels: output always starts with '{'", "[prometheus_format]")
{
    char buf[64];
    const char *keys[]   = {"instance"};
    const char *values[] = {"bitaxe-601"};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 1);
    TEST_ASSERT_NOT_EQUAL('\0', buf[0]);
    TEST_ASSERT_EQUAL('{', buf[0]);
}

TEST_CASE("format_labels: output always ends with '}'", "[prometheus_format]")
{
    char buf[64];
    const char *keys[]   = {"instance"};
    const char *values[] = {"bitaxe-601"};
    prometheus_format_labels(buf, sizeof(buf), keys, values, 1);
    size_t len = strlen(buf);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL('}', buf[len - 1]);
}
