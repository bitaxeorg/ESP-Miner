#ifndef BM_JOB_PACKET_H_
#define BM_JOB_PACKET_H_

#include "asic_job.h"
#include "asic_job_slot.h"
#include "asic_common.h"

#define BM1397_NUM_MIDSTATES 4

/* Wire payload shared by BM1366, BM1368, BM1370, and BM1373. All integers
 * are byte arrays so encoding also works at unaligned packet addresses. */
typedef struct __attribute__((__packed__)) {
    uint8_t chip_slot;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
} bm13xx_job_packet_t;

typedef struct __attribute__((__packed__)) {
    uint8_t chip_slot;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle4[4];
    uint8_t midstates[BM1397_NUM_MIDSTATES][32];
} bm1397_job_packet_t;

/* Pure wire encoders: serializes the job into the wire payload. The caller supplies
 * valid, non-overlapping storage; neither function retains pointers or modifies state. */
void bm13xx_encode_job_packet(const asic_job_t *job, uint8_t chip_slot,
                              bm13xx_job_packet_t *packet);
void bm1397_encode_job_packet(const asic_job_t *job, uint8_t chip_slot,
                              uint8_t software_midstates,
                              bm1397_job_packet_t *packet);

#define BM13XX_JOB_SLOT_COUNT 16
#define BM1397_JOB_SLOT_COUNT 32

#define BM13XX_TX_SLOT_SHIFT 3
#define BM1397_TX_SLOT_SHIFT 2

static inline uint8_t bm_encode_wire_job_slot(uint8_t job_slot, uint8_t tx_shift)
{
    return (uint8_t)(job_slot << tx_shift);
}

static inline uint8_t bm_decode_wire_job_slot(uint8_t wire_byte, uint8_t rx_shift)
{
    return (uint8_t)(wire_byte >> rx_shift);
}

static inline uint8_t bm_decode_wire_sub_id(uint8_t wire_byte, uint8_t sub_id_bits)
{
    return (uint8_t)(wire_byte & ((1U << sub_id_bits) - 1));
}

/* Driver-level job packet builders: claims a job slot, maps it to chip_slot,
 * and encodes the wire packet. */
void bm13xx_build_job_packet(asic_job_store_t *store, const asic_job_t *job,
                             bm13xx_job_packet_t *packet);
void bm1397_build_job_packet(asic_job_store_t *store, uint8_t software_midstates,
                             const asic_job_t *job, bm1397_job_packet_t *packet);

/* Job result wire packet decoders: resolves the active job from wire chip_slot,
 * extracts nonce / rolled version / ASIC indices, and records result metadata. */
bool bm13xx_decode_job_result(asic_job_store_t *store,
                              uint8_t small_core_bits,
                              uint16_t address_interval,
                              uint32_t wire_nonce,
                              uint8_t wire_chip_slot,
                              uint16_t wire_version,
                              task_result *result);

bool bm1397_decode_job_result(asic_job_store_t *store,
                              uint16_t address_interval,
                              uint32_t wire_nonce,
                              uint8_t wire_chip_slot,
                              task_result *result);

void bm13xx_clear_work(asic_job_store_t *store);
void bm13xx_init_job_slots(asic_job_store_t *store, uint16_t capacity);
void bm13xx_destroy_job_slots(asic_job_store_t *store);

#endif
