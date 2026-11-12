#define _POSIX_C_SOURCE 200809L
#include "bzm/platform.h"
#include "bzm/posix.h"
#include <errno.h>
#include <hash_impl.h>
#include <stdio.h>
#include <time.h>

static int64_t now_us(void *ctx)
{
    (void)ctx;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
static void sleep_us(void *ctx, uint32_t us)
{
    (void)ctx;
    struct timespec delay = {.tv_sec = us / 1000000U, .tv_nsec = (long)(us % 1000000U) * 1000};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
}
static void log_message(void *ctx, char level, const char *tag, const char *format, va_list args)
{
    (void)ctx;
    if (level == 'D' || level == 'V') return;
    fprintf(stderr, "%c %s: ", level, tag);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
}

static bzm_status_t sha256(void *ctx, const uint8_t *data, size_t size, uint8_t digest[32])
{
    (void)ctx;
    if (digest == NULL || (data == NULL && size != 0)) return BZM_ERR_INVALID_ARG;
    secp256k1_sha256 hash;
    secp256k1_sha256_initialize(&hash);
    if (size != 0) secp256k1_sha256_write(&hash, data, size);
    secp256k1_sha256_finalize(&hash, digest);
    secp256k1_sha256_clear(&hash);
    return BZM_OK;
}

void bzm_platform_posix_defaults(bzm_platform_ops_t *ops)
{
    *ops = (bzm_platform_ops_t){.now_us = now_us, .sleep_us = sleep_us, .log = log_message, .sha256 = sha256};
}
