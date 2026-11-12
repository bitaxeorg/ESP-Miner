#include "bzm/platform_serial.h"
#include "bzm/platform.h"
#include <pthread.h>

bzm_status_t bzm_port_serial_init(void) { return bzm_serial_init(); }
bool bzm_port_serial_is_initialized(void) { return bzm_serial_is_initialized(); }
bzm_status_t bzm_port_serial_set_baud(int baud) { return bzm_serial_set_baud(baud); }
bzm_status_t bzm_port_serial_wait_tx_done(uint32_t timeout_ms) { return bzm_serial_wait_tx(timeout_ms); }
bzm_status_t bzm_port_serial_ensure_initialized(int baud)
{
    if (!bzm_port_serial_is_initialized()) {
        bzm_status_t status = bzm_port_serial_init();
        if (status != BZM_OK) return status;
    }
    return bzm_port_serial_set_baud(baud);
}
bzm_status_t bzm_port_serial_prepare_session(int baud)
{
    bzm_status_t status = bzm_port_serial_ensure_initialized(baud);
    return status == BZM_OK ? bzm_serial_clear_rx() : status;
}
int bzm_port_serial_send(uint8_t *data, int len, bool debug)
{
    if (data == NULL || len < 0) return -1;
    if (debug) bzm_log_hex("bzm", data, (size_t)len);
    return bzm_serial_write(data, (size_t)len);
}
int16_t bzm_port_serial_rx(uint8_t *data, uint16_t size, uint16_t timeout_ms)
{
    return bzm_serial_read(data, size, timeout_ms);
}
void bzm_port_serial_clear_buffer(void) { (void)bzm_serial_clear_rx(); }

static pthread_mutex_t transport_lock = PTHREAD_MUTEX_INITIALIZER;
static int16_t transport_read(void *context, uint8_t *data, uint16_t size, uint16_t timeout_ms)
{
    (void)context;
    return bzm_port_serial_rx(data, size, timeout_ms);
}
static int transport_write(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    return bzm_port_serial_send((uint8_t *)data, (int)size, false);
}
static void transport_clear_rx(void *context)
{
    (void)context;
    bzm_port_serial_clear_buffer();
}
static uint64_t transport_now_us(void *context)
{
    (void)context;
    return (uint64_t)bzm_now_us();
}
static void transport_lock_io(void *context)
{
    (void)context;
    pthread_mutex_lock(&transport_lock);
}
static void transport_unlock_io(void *context)
{
    (void)context;
    pthread_mutex_unlock(&transport_lock);
}
const bzm_serial_ops_t bzm_platform_serial_ops = {
    .read = transport_read,
    .write = transport_write,
    .clear_rx = transport_clear_rx,
    .now_us = transport_now_us,
    .lock = transport_lock_io,
    .unlock = transport_unlock_io,
};
