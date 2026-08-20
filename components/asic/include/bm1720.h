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
#define BM1720_JOB_TYPE            0x20  /* CONFIRMED (A3): work/job frame */

#define BM1720_CMD_SET_ADDRESS     0x00  /* CONFIRMED (R3 + A3)            */
#define BM1720_CMD_WRITE_REG       0x01  /* CONFIRMED (R3 + A3)            */
#define BM1720_CMD_READ_REG        0x02  /* CONFIRMED (A3)                 */
#define BM1720_CMD_CHAIN_INACTIVE  0x03  /* CONFIRMED (R3 + A3)            */

/* Frame lengths, in bytes, including the trailing CRC5. */
#define BM1720_VIL_LEN_SHORT       5   /* no payload: addr, inactive, read */
#define BM1720_VIL_LEN_REG         9   /* 4-byte register payload          */

/* Work/job frame (CONFIRMED against the A3 send-work thread @0x41c90):
 *   [0]      BM1720_JOB_TYPE (0x20)
 *   [1]      work id, 7 bits (work->id & 0x7f)
 *   [2..81]  the 80-byte Sia/Blake2b block header, each 32-bit word byte-
 *            swapped to big-endian
 *   [82..83] CRC16/CCITT-FALSE (poly 0x1021, init 0xffff) over bytes [0..81],
 *            transmitted high byte first
 * The chip hashes the raw header (no host midstate) and enumerates the nonce
 * itself; the frame carries no chip address, so it is broadcast to the whole
 * chain and each ASIC self-partitions the nonce space set at enumeration. */
#define BM1720_JOB_HEADER_LEN      80  /* Sia block header bytes           */
#define BM1720_JOB_ID_MASK         0x7f
#define BM1720_JOB_FRAME_LEN       84  /* type + id + header + CRC16        */

/* UART responses are 9 bytes: a 2-byte 0xAA 0x55 preamble (mirror of the TX
 * 0x55 0xAA) followed by a 7-byte body [nonce(4), byte5, byte6, flags|crc5].
 * CONFIRMED from the A3 receive collector (@0x46978) and CRC check: CRC5 runs
 * over the first 51 bits of the body and lives in the low 5 bits of the final
 * byte; bit 7 of that byte flags a non-data response. */
#define BM1720_RESP_LEN            9

/* Registers addressed by WRITE_REG / READ_REG.
 * All five are now CONFIRMED against the A3 cgminer-sia binary: the register
 * addresses are proven both from the write-frame builder (0x42320, reg byte
 * in the frame) and from the read-back dispatcher (0x4729c), which switches
 * on the register-address byte the chip echoes and selects a matching log
 * string ("CHIP_ADDR", "PLL_PARAMETER", "MISC_CONTROL", "TICK_MASK",
 * "GENERAL_I2C_COMMAND"). MISC_CONTROL (0x1c) and GENERAL_I2C (0x20) were
 * originally guessed from the BM1387 map and turn out to be exactly right.
 * The A3 additionally writes registers 0x28 and 0x2c broadcast during
 * bring-up; their function is still unknown. */
#define BM1720_REG_CHIP_ADDR       0x00  /* CONFIRMED (A3)                 */
#define BM1720_REG_HASH_RATE       0x08  /* CONFIRMED read-back (A3), status */
#define BM1720_REG_PLL_PARAM       0x0c  /* CONFIRMED                      */
#define BM1720_REG_TICKET_MASK     0x14  /* CONFIRMED                      */
#define BM1720_REG_MISC_CONTROL    0x1c  /* CONFIRMED (A3)                 */
#define BM1720_REG_GENERAL_I2C     0x20  /* CONFIRMED (A3)                 */
#define BM1720_REG_CORE_MUX_SELECT 0x40  /* CONFIRMED (A3): temp/vdd mux   */

/* Bring-up registers written broadcast by the A3, function not yet known. */
#define BM1720_REG_UNKNOWN_28      0x28  /* CONFIRMED write (A3)           */
#define BM1720_REG_UNKNOWN_2C      0x2c  /* CONFIRMED write (A3)           */

/* ------------------------------------------------------------------ *
 * Transport
 * ------------------------------------------------------------------ *
 *
 * Both firmware images that carry this chip drive it over a plain UART, not
 * an FPGA bridge: the A3 opens a per-chain tty (open/tcsetattr/write with
 * flock serialization, send helper @0x41b94) exactly like the R3 does.
 * CONFIRMED baud: 115200 or 57600 (the A3 baud->termios mapper @0x41918
 * accepts both). The transport is a vtable so the same command layer can sit
 * on whatever tty/driver the host provides.
 *
 * CONFIRMED (both R3 and A3): every TX frame is prepended with the two
 * preamble bytes 0x55 0xAA before it hits the wire. The earlier note that
 * the A3 took bare frames was wrong - set uart_preamble = true for the A3
 * too.
 *
 * NOTE the RX direction uses the MIRRORED preamble: responses from the chip
 * begin 0xAA 0x55 (byte 0xAA first), not 0x55 0xAA. The A3 receive collector
 * (@0x46978) syncs on 0xAA-then-0x55. bm1720_parse_nonce() checks for that
 * order; do not confuse it with the TX preamble above.
 */

#define BM1720_PREAMBLE_0          0x55  /* TX: first byte on the wire     */
#define BM1720_PREAMBLE_1          0xaa  /* TX: second byte                */
#define BM1720_RESP_PREAMBLE_0     0xaa  /* RX: responses start 0xAA ...   */
#define BM1720_RESP_PREAMBLE_1     0x55  /* RX: ... then 0x55              */

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
	bool uart_preamble;   /* prepend 0x55 0xAA to every TX frame         */
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
 * defaults to 256 / next_pow2(chip_count), which is what the A3 firmware
 * computes (calculate_address_interval @0x417c8: it rounds the chip count up
 * to a power of two before dividing 256 by it). */
int bm1720_chain_inactive(struct bm1720_chain *chain);
int bm1720_set_address(struct bm1720_chain *chain, uint8_t chip_addr);
int bm1720_enumerate(struct bm1720_chain *chain);

/* Look up the PLL words for a frequency. Returns the table index, or -1 when
 * the frequency is not tabulated. Either output pointer may be NULL. */
int bm1720_lookup_pll(int freq_mhz, uint32_t *vilpll, uint32_t *fildiv1,
		      uint32_t *fildiv2);

/* Compute PLL words for an arbitrary frequency not present in the table.
 * The A3 itself only does exact table lookups (get_plldata @0x3b6bc); this is
 * a convenience fallback that stays inside the table's clean high-frequency
 * operating envelope (refdiv 2, postdiv1 2..4, postdiv2 1 - the encoding used
 * by every table row at and above ~200 MHz) and refuses requests more than
 * about 2% away from anything reachable. For exact-silicon fidelity prefer
 * bm1720_lookup_pll(); the low-frequency rows use a different divider
 * encoding that this fallback deliberately does not synthesise. */
int bm1720_compute_pll(int freq_mhz, uint32_t *vilpll, uint32_t *fildiv1,
		       uint32_t *fildiv2);

/* Program the chain frequency (broadcast WRITE_REG of PLL_PARAM). */
int bm1720_set_frequency(struct bm1720_chain *chain, int freq_mhz);

/* Ticket mask controls which nonces the chip bothers returning
 * (broadcast WRITE_REG of TICKET_MASK). */
int bm1720_set_ticket_mask(struct bm1720_chain *chain, uint32_t mask);

/* Read a register back from one chip. buf must hold at least
 * BM1720_RESP_LEN bytes; on a UART transport the response arrives with its
 * 0xAA 0x55 preamble included. */
int bm1720_read_register(struct bm1720_chain *chain, uint8_t chip, uint8_t reg,
			 uint8_t *buf, int timeout_ms);

/* Build a work/job frame into buf (which must hold at least
 * BM1720_JOB_FRAME_LEN bytes) and stamp the trailing CRC16. header is the
 * 80-byte Sia/Blake2b block header in host byte order; each 32-bit word is
 * byte-swapped to big-endian as it is copied in. work_id is masked to 7 bits.
 * Returns the frame length (BM1720_JOB_FRAME_LEN) or -1 on bad arguments. */
int bm1720_build_job(uint8_t *buf, size_t buf_len,
		     const uint8_t *header, uint8_t work_id);

/* Broadcast one work item to the whole chain. header is the 80-byte Sia
 * header in host byte order; work_id tags the nonces the chip returns for it
 * (see bm1720_parse_nonce, the wc field). */
int bm1720_send_work(struct bm1720_chain *chain,
		     const uint8_t *header, uint8_t work_id);

/* ------------------------------------------------------------------ *
 * Response / nonce return
 * ------------------------------------------------------------------ */

/* A decoded 9-byte response.
 * CONFIRMED against the A3 receive collector (@0x46978) and nonce consumer
 * (bitmain_scanhash @0x3c604): responses start 0xAA 0x55, carry a 32-bit
 * big-endian nonce (or register value on reads), and end in a byte whose low
 * 5 bits are a CRC5 over the body's first 51 bits and whose bit 7 flags a
 * non-data (register-read) response. Byte 6 is a difficulty/status byte and
 * byte 7 (masked to 7 bits) is the work index "wc" that selects which queued
 * job the nonce solves - the chain id is taken from the receiving UART, not
 * the frame. The 64-bit Sia header nonce is completed host-side (stratum
 * extranonce); the chip only ever returns this 32-bit field. */
struct bm1720_nonce {
	uint32_t nonce;   /* bytes 2..5, big-endian                         */
	uint8_t diff;     /* byte 6: difficulty/status byte                 */
	uint8_t wc;       /* byte 7 & 0x7f: work index the nonce solves     */
	uint8_t flags;    /* byte 8 bits 7..5                               */
};

/* Returns 0 on a valid data response, 1 when the response is flagged
 * (bit 7 of the last byte set - the firmware routes these separately
 * without checking the CRC), -1 on bad header/CRC/length. */
int bm1720_parse_nonce(const uint8_t *buf, size_t len, struct bm1720_nonce *out);

#endif /* BM1720_H */
