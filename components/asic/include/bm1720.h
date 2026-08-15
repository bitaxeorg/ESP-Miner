/* bm1720.h - driver interface for the Bitmain BM1720 mining ASIC.
 *
 * Reconstructed from shipped firmware, not from vendor sources. Every frame
 * layout and opcode below has been cross-checked against BOTH firmware images
 * that drive this chip:
 *
 *   R3: usr/bin/cgminer from AntRouterR3LTCSIADASH20180815.bin
 *       (MIPS32 BE, UART transport; --sia selects chiptype 1720)
 *   A3: usr/bin/cgminer from AntminerA32018111311360M.tar.gz
 *       (ARM LE, FPGA-bridge transport, BM1720 hash boards)
 *
 * Items marked CONFIRMED were read out of the disassembly of at least one of
 * those binaries (addresses in VALIDATION.md). Items marked INFERRED have not
 * been validated against real silicon or firmware.
 */

#ifndef BM1720_H
#define BM1720_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ *
 * Chip identity
 * ------------------------------------------------------------------ */

/* get_plldata() in the AntRouter cgminer dispatches on these values.
 * CONFIRMED: 1387, 1485, 1720 and 1760 are compared as literals
 * (R3 @0x44cadc..0x44cb04), and --sia selects chiptype 1720
 * (R3 @0x455810/0x455814), which selects the 99-entry PLL table
 * reproduced in bm1720_pll_table.h. */
#define BM1720_CHIPTYPE            1720

/* Reference clock feeding the on-chip PLL. INFERRED from the divider
 * arithmetic: 25 MHz is the only value that makes all 99 table entries
 * reproduce their own labels. */
#define BM1720_REF_CLK_MHZ         25

/* ------------------------------------------------------------------ *
 * Command framing
 * ------------------------------------------------------------------ *
 *
 * A BM1720 command frame is
 *
 *     [0] type   : 0x40 | (all << 4) | cmd
 *     [1] length : total frame length in bytes, CRC included
 *     [2] chip   : chip address (0 when the "all" bit is set)
 *     [3] reg    : register address, or 0 for commands without one
 *     [4..n-2]   : payload, big-endian
 *     [n-1]      : CRC5 in the low 5 bits
 *
 * CONFIRMED: frame lengths 5 (no payload) and 9 (4-byte register payload),
 * and the command opcodes below, from the builders in both firmwares:
 *
 *   SET_ADDRESS     [0x40 0x05 addr 0x00 crc]   R3 @0x456750, A3 @0x610d8
 *   WRITE_REGISTER  [0x51 0x09 0x00 reg  v4 crc] broadcast, or 0x41 with a
 *                   chip address for unicast     R3 @0x454924, A3 @0x60854
 *   READ_REGISTER   [0x42/0x52 0x05 chip reg crc]              A3 @0x6078c
 *   CHAIN_INACTIVE  [0x53 0x05 0x00 0x00 crc]   R3 @0x456560, A3 @0x6104c
 *
 * NOTE: this is NOT the BM1387/BM1485 VIL opcode map. The same R3 binary
 * contains that older map too (0x41 set-addr, 0x55 chain-inactive, 0x58
 * set-config) but only on its BM1485/scrypt code paths. On the BM1720,
 * 0x41 means "unicast register write" and 0x52 means "broadcast register
 * read" - reusing the 1387 opcodes here silently issues the wrong commands.
 */

#define BM1720_VIL_TYPE            0x40  /* high nibble marks a command    */
#define BM1720_VIL_ALL             0x10  /* broadcast to every chip        */

#define BM1720_CMD_SET_ADDRESS     0x00  /* CONFIRMED (R3 + A3)            */
#define BM1720_CMD_WRITE_REG       0x01  /* CONFIRMED (R3 + A3)            */
#define BM1720_CMD_READ_REG        0x02  /* CONFIRMED (A3)                 */
#define BM1720_CMD_CHAIN_INACTIVE  0x03  /* CONFIRMED (R3 + A3)            */

/* Frame lengths, in bytes, including the trailing CRC5. */
#define BM1720_VIL_LEN_SHORT       5   /* no payload: addr, inactive, read */
#define BM1720_VIL_LEN_REG         9   /* 4-byte register payload          */

/* UART responses are 9 bytes: a 2-byte 0x55 0xAA preamble followed by a
 * 7-byte body [data(4), byte5, byte6, flags|crc5]. CONFIRMED from the R3
 * response validator (@0x44f078): CRC5 runs over the first 51 bits of the
 * body and lives in the low 5 bits of the final byte; bit 7 of that byte
 * flags a non-data response. */
#define BM1720_RESP_LEN            9

/* Registers addressed by WRITE_REG / READ_REG.
 * PLL_PARAM and TICKET_MASK are CONFIRMED in both firmwares (R3 @0x454940
 * and @0x454e74; A3 @0x61708 and @0x6189c). The A3 additionally writes
 * registers 0x28 and 0x2c broadcast during bring-up (@0x61378/0x613bc);
 * their function is unknown. The remaining offsets are INFERRED from the
 * BM1387/BM1485 register map - treat as a starting point to probe. */
#define BM1720_REG_CHIP_ADDR       0x00  /* INFERRED                       */
#define BM1720_REG_PLL_PARAM       0x0c  /* CONFIRMED                      */
#define BM1720_REG_TICKET_MASK     0x14  /* CONFIRMED                      */
#define BM1720_REG_MISC_CONTROL    0x1c  /* INFERRED                       */
#define BM1720_REG_GENERAL_I2C     0x20  /* INFERRED                       */

/* ------------------------------------------------------------------ *
 * Transport
 * ------------------------------------------------------------------ *
 *
 * Two transports appear across the two firmware images that carry this chip:
 * a plain UART on the AntRouter R3 (CONFIRMED: 115200 or 57600 baud, rejected
 * otherwise by the option parser) and a memory-mapped FPGA bridge on the
 * Antminer A3. Only the framing above is shared, so the transport is a
 * vtable rather than being baked into the command layer.
 *
 * CONFIRMED: on the R3's UART, every command frame is prepended with the
 * two preamble bytes 0x55 0xAA before it hits the wire (set_pll, set_addr
 * and chain_inactive builders all do this). The A3's FPGA bridge takes the
 * bare frame. Set uart_preamble accordingly.
 */

#define BM1720_PREAMBLE_0          0x55
#define BM1720_PREAMBLE_1          0xaa

struct bm1720_transport {
	int (*write)(void *ctx, const uint8_t *buf, size_t len);
	int (*read)(void *ctx, uint8_t *buf, size_t len, int timeout_ms);
	void *ctx;
};

struct bm1720_chain {
	struct bm1720_transport tp;
	int chip_count;       /* chips enumerated by bm1720_enumerate()      */
	int addr_interval;    /* address step between chips on the chain     */
	int freq_mhz;         /* frequency last programmed                   */
	bool uart_preamble;   /* prepend 0x55 0xAA to every TX frame (R3)    */
};

/* ------------------------------------------------------------------ *
 * CRC
 * ------------------------------------------------------------------ */

/* CRC5 over a command frame, x^5 + x^2 + 1, initial register all ones,
 * MSB of the register transmitted as bit 4 of the result. Matches the
 * implementations disassembled from both firmwares (R3 @0x44ef84,
 * A3 @0x592f8). The length is in BITS, matching Bitmain's own callers,
 * which pass (frame_len - 1) * 8 so the CRC byte itself is excluded. */
uint8_t bm1720_crc5(const uint8_t *data, size_t bit_len);

/* CRC16 (CCITT-FALSE) as used to protect work payloads. */
uint16_t bm1720_crc16(const uint8_t *data, size_t len);

/* ------------------------------------------------------------------ *
 * Command layer
 * ------------------------------------------------------------------ */

/* Build a command frame into buf (which must hold at least len bytes) and
 * stamp the CRC5. Returns the frame length, or -1 if the arguments don't
 * fit. The UART preamble, when needed, is the transport's business and is
 * added by bm1720_send_vil(). */
int bm1720_build_vil(uint8_t *buf, size_t buf_len, uint8_t cmd, bool all,
		     uint8_t chip, uint8_t reg,
		     const uint8_t *payload, size_t payload_len);

/* Send one command. payload may be NULL. */
int bm1720_send_vil(struct bm1720_chain *chain, uint8_t cmd, bool all,
		    uint8_t chip, uint8_t reg,
		    const uint8_t *payload, size_t payload_len);

/* Chain bring-up: broadcast CHAIN_INACTIVE, then walk the chain handing out
 * addresses in steps of addr_interval. If addr_interval is 0 or negative it
 * defaults to 256 / chip_count, which is what the R3 firmware computes
 * (@0x4563ec). */
int bm1720_chain_inactive(struct bm1720_chain *chain);
int bm1720_set_address(struct bm1720_chain *chain, uint8_t chip_addr);
int bm1720_enumerate(struct bm1720_chain *chain);

/* Look up the PLL words for a frequency. Returns the table index, or -1 when
 * the frequency is not tabulated. Either output pointer may be NULL. */
int bm1720_lookup_pll(int freq_mhz, uint32_t *vilpll, uint32_t *fildiv1,
		      uint32_t *fildiv2);

/* Compute PLL words for an arbitrary frequency instead of looking one up.
 * Searches only divider combinations the shipped table actually uses
 * (refdiv 2, postdiv1 2..4, postdiv2 1) and refuses requests more than
 * about 2% away from anything reachable. */
int bm1720_compute_pll(int freq_mhz, uint32_t *vilpll, uint32_t *fildiv1,
		       uint32_t *fildiv2);

/* Program the chain frequency (broadcast WRITE_REG of PLL_PARAM). */
int bm1720_set_frequency(struct bm1720_chain *chain, int freq_mhz);

/* Ticket mask controls which nonces the chip bothers returning
 * (broadcast WRITE_REG of TICKET_MASK). */
int bm1720_set_ticket_mask(struct bm1720_chain *chain, uint32_t mask);

/* Read a register back from one chip. buf must hold at least
 * BM1720_RESP_LEN bytes; on a UART transport the response arrives with its
 * 0x55 0xAA preamble included. */
int bm1720_read_register(struct bm1720_chain *chain, uint8_t chip, uint8_t reg,
			 uint8_t *buf, int timeout_ms);

/* ------------------------------------------------------------------ *
 * Response / nonce return
 * ------------------------------------------------------------------ */

/* A decoded 9-byte response.
 * CONFIRMED (R3 @0x44f53c/@0x44f078): responses start 0x55 0xAA, carry a
 * 32-bit big-endian data word (the nonce, or the register value on reads),
 * and end in a byte whose low 5 bits are a CRC5 over the body's first 51
 * bits and whose bit 7 flags a non-data response. On register reads byte 6
 * is the chip address and byte 7 the register address; their meaning inside
 * nonce returns is INFERRED. */
struct bm1720_nonce {
	uint32_t nonce;   /* bytes 2..5, big-endian                         */
	uint8_t chip;     /* byte 6: chip address on register reads         */
	uint8_t reg;      /* byte 7: register address on register reads     */
	uint8_t flags;    /* byte 8 bits 7..5                               */
};

/* Returns 0 on a valid data response, 1 when the response is flagged
 * (bit 7 of the last byte set - the firmware routes these separately
 * without checking the CRC), -1 on bad header/CRC/length. */
int bm1720_parse_nonce(const uint8_t *buf, size_t len, struct bm1720_nonce *out);

#endif /* BM1720_H */
