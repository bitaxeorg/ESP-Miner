#ifndef BM13XX_FIXTURE_H
#define BM13XX_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "asic_common.h"

typedef struct GlobalState GlobalState;

enum { BM13XX_FIXTURE_DRIVER_COUNT = 4, BM13XX_FIXTURE_RESPONSE_SIZE = 11 };

typedef struct {
    const char *name;
    uint8_t (*init)(GlobalState *state);
    void (*set_version_mask)(uint32_t mask);
    task_result *(*process_work)(GlobalState *state);
    uint8_t response_job_id;
    uint8_t init_mask_count;
} bm13xx_fixture_driver_t;

typedef struct {
    uint8_t bytes[128];
    size_t length;
} bm13xx_fixture_packet_t;

extern const bm13xx_fixture_driver_t
    bm13xx_fixture_drivers[BM13XX_FIXTURE_DRIVER_COUNT];

GlobalState *bm13xx_fixture_begin(void);
void bm13xx_fixture_end(void);
void bm13xx_fixture_clear_packets(void);
size_t bm13xx_fixture_packet_count(void);
const bm13xx_fixture_packet_t *bm13xx_fixture_packet(size_t index);
void bm13xx_fixture_fail_writes(unsigned count);
unsigned bm13xx_fixture_delay_count(void);
void bm13xx_fixture_set_job(uint32_t version, bool valid, bool present);
/* Supply bytes after receive_work's framing/CRC check; NULL means timeout. */
void bm13xx_fixture_queue_response(
    const uint8_t response[BM13XX_FIXTURE_RESPONSE_SIZE]);

#endif
