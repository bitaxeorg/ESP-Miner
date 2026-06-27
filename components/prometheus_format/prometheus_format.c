#include "prometheus_format.h"

#include <string.h>
#include <stdio.h>

void prometheus_escape_label_value(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_size; ++i) {
        char c = src[i];
        if (c == '\\' || c == '"') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n' || c == '\r') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if ((unsigned char)c < 32 || (unsigned char)c > 126) {
            dst[j++] = '_';
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

void prometheus_copy_sanitized_ascii(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_size; ++i) {
        unsigned char c = (unsigned char)src[i];
        dst[j++] = (c < 32 || c > 126) ? '_' : (char)c;
    }
    dst[j] = '\0';
}

void prometheus_format_label(char *buf, size_t bufsize, const char *key, const char *value) {
    char esc[128];
    prometheus_escape_label_value(value, esc, sizeof(esc));
    snprintf(buf, bufsize, "%s=\"%s\"", key, esc);
}

void prometheus_format_labels(char *buf, size_t bufsize,
                               const char **keys, const char **values, int count) {
    size_t pos = 0;
    if (count <= 0) {
        buf[0] = '\0';
        return;
    }
    if (pos < bufsize) buf[pos++] = '{';
    for (int i = 0; i < count; ++i) {
        char pair[192];
        prometheus_format_label(pair, sizeof(pair), keys[i], values[i]);
        size_t pairlen = strlen(pair);
        if (pos + pairlen + 2 >= bufsize) break;
        memcpy(buf + pos, pair, pairlen);
        pos += pairlen;
        if (i != count - 1) buf[pos++] = ',';
    }
    if (pos < bufsize) buf[pos++] = '}';
    buf[pos < bufsize ? pos : bufsize - 1] = '\0';
}
