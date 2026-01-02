/**
 * @file fake_time.c
 * @brief Controllable time implementation
 */

#include "fake_time.h"
#include <time.h>

static int64_t g_fake_time_us = 0;
static uint32_t g_fake_ticks = 0;
static bool g_fake_time_enabled = false;

void fake_time_init(void)
{
    g_fake_time_us = 0;
    g_fake_ticks = 0;
    g_fake_time_enabled = true;
}

void fake_time_set_us(int64_t time_us)
{
    g_fake_time_us = time_us;
    /* Also update ticks (assuming 1ms per tick) */
    g_fake_ticks = (uint32_t)(time_us / 1000);
}

int64_t fake_time_get_us(void)
{
    if (!g_fake_time_enabled) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
    }
    return g_fake_time_us;
}

void fake_time_advance_us(int64_t delta_us)
{
    g_fake_time_us += delta_us;
    g_fake_ticks = (uint32_t)(g_fake_time_us / 1000);
}

void fake_time_advance_ms(int64_t delta_ms)
{
    fake_time_advance_us(delta_ms * 1000);
}

void fake_time_advance_sec(int64_t delta_sec)
{
    fake_time_advance_us(delta_sec * 1000000);
}

void fake_time_set_ticks(uint32_t ticks)
{
    g_fake_ticks = ticks;
    g_fake_time_us = (int64_t)ticks * 1000;
}

uint32_t fake_time_get_ticks(void)
{
    if (!g_fake_time_enabled) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
    }
    return g_fake_ticks;
}

void fake_time_enable(bool enable)
{
    g_fake_time_enabled = enable;
}

bool fake_time_is_enabled(void)
{
    return g_fake_time_enabled;
}
