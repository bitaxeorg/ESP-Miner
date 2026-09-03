#pragma once

#include <stddef.h>

/**
 * @brief Escape a Prometheus label value per the text exposition format.
 *
 * Escapes backslashes, double-quotes, and newlines. Output is always NUL-terminated.
 */
void prometheus_escape_label_value(const char *src, char *dst, size_t dst_size);

/**
 * @brief Copy src into dst, keeping only printable ASCII (0x20–0x7E). NUL-terminates.
 */
void prometheus_copy_sanitized_ascii(char *dst, size_t dst_size, const char *src);

/**
 * @brief Format a single key="escaped_value" pair WITHOUT surrounding braces.
 *
 * This is an internal building block used by prometheus_format_labels().
 * Do NOT pass the output directly to prometheus_write_metric(); use
 * prometheus_format_labels() instead to get the required {…} wrapper.
 */
void prometheus_format_label(char *buf, size_t bufsize, const char *key, const char *value);

/**
 * @brief Format a Prometheus label set as {key1="v1",key2="v2",...}.
 *
 * Produces the complete {…} block required by the text exposition format.
 * Pass the result directly to prometheus_write_metric() as the `labels` argument.
 *
 * @param buf      Output buffer.
 * @param bufsize  Size of output buffer.
 * @param keys     Array of label key strings.
 * @param values   Array of label value strings (will be escaped).
 * @param count    Number of labels; if 0 buf is set to empty string.
 */
void prometheus_format_labels(char *buf, size_t bufsize,
                               const char **keys, const char **values, int count);
