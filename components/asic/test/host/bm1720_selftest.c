/* bm1720_selftest.c - host-side self test for the BM1720 driver.
 *
 * All expected byte sequences below were read out of the disassembly of the
 * two shipped firmwares (AntRouter R3 --sia path, Antminer A3), so this test
 * pins the driver to what those binaries actually put on the wire.
 *
 * Build:  cc -Wall -Wextra -o bm1720_selftest bm1720_selftest.c bm1720.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm1720.h"
#include "bm1720_pll_table.h"

static int failures;

#define CHECK(cond, ...) do {						\
	if (!(cond)) {							\
		failures++;						\
		printf("FAIL: " __VA_ARGS__);				\
		printf("\n");						\
	}								\
} while (0)

struct capture {
	uint8_t buf[64];
	size_t len;
};

static int cap_write(void *ctx, const uint8_t *buf, size_t len)
{
	struct capture *c = ctx;

	if (len > sizeof(c->buf))
		return -1;
	memcpy(c->buf, buf, len);
	c->len = len;
	return (int)len;
}

static void expect_frame(struct capture *c, const uint8_t *want, size_t n,
			 const char *what)
{
	CHECK(c->len == n, "%s: sent %zu bytes, want %zu", what, c->len, n);
	if (c->len == n)
		CHECK(!memcmp(c->buf, want, n), "%s: frame bytes differ", what);
}

int main(void)
{
	struct capture cap = { {0}, 0 };
	struct bm1720_chain chain;
	struct bm1720_nonce nn;
	uint8_t resp[BM1720_RESP_LEN];
	uint32_t v;
	size_t i;

	memset(&chain, 0, sizeof(chain));
	chain.tp.write = cap_write;
	chain.tp.ctx = &cap;
	chain.chip_count = 1;

	/* -- frames as the A3 sends them (no preamble) ------------------ */

	/* A3 @0x610d8: set address 0 */
	{
		const uint8_t want[] = { 0x40, 0x05, 0x00, 0x00, 0x1c };
		bm1720_set_address(&chain, 0x00);
		expect_frame(&cap, want, sizeof(want), "set_address(0)");
	}

	/* A3 @0x6104c: chain inactive */
	{
		const uint8_t want[] = { 0x53, 0x05, 0x00, 0x00, 0x03 };
		bm1720_chain_inactive(&chain);
		expect_frame(&cap, want, sizeof(want), "chain_inactive");
	}

	/* A3 @0x60854 via set_frequency: broadcast write reg 0x0c */
	{
		const uint8_t want[] = { 0x51, 0x09, 0x00, 0x0c,
					 0x00, 0x50, 0x02, 0x21, 0x0c };
		bm1720_set_frequency(&chain, 500);
		expect_frame(&cap, want, sizeof(want), "set_frequency(500)");
	}

	/* A3 @0x60854 via set_ticket_mask: broadcast write reg 0x14 */
	{
		uint8_t want[] = { 0x51, 0x09, 0x00, 0x14,
				   0x00, 0x00, 0x00, 0x0f, 0x00 };
		want[8] = bm1720_crc5(want, 64);
		bm1720_set_ticket_mask(&chain, 0x0f);
		expect_frame(&cap, want, sizeof(want), "set_ticket_mask(0xf)");
	}

	/* -- the same commands over R3-style UART gain the preamble ----- */

	chain.uart_preamble = true;
	{
		const uint8_t want[] = { 0x55, 0xaa,
					 0x53, 0x05, 0x00, 0x00, 0x03 };
		bm1720_chain_inactive(&chain);
		expect_frame(&cap, want, sizeof(want),
			     "chain_inactive w/ preamble");
	}
	chain.uart_preamble = false;

	/* -- response parsing ------------------------------------------- */

	/* forge a valid response the way the A3 collector (@0x46978) checks
	 * it: AA 55 preamble (mirror of the TX 55 AA), CRC5 over the body's
	 * first 51 bits in the low 5 bits of the last byte */
	resp[0] = 0xaa; resp[1] = 0x55;
	resp[2] = 0x12; resp[3] = 0x34; resp[4] = 0x56; resp[5] = 0x78;
	resp[6] = 0x04; resp[7] = 0x0c; resp[8] = 0;
	resp[8] = bm1720_crc5(resp + 2, 51);

	CHECK(bm1720_parse_nonce(resp, sizeof(resp), &nn) == 0,
	      "parse_nonce: valid frame rejected");
	CHECK(nn.nonce == 0x12345678, "parse_nonce: nonce 0x%08x", nn.nonce);
	CHECK(nn.diff == 0x04 && nn.wc == 0x0c,
	      "parse_nonce: diff/wc %02x/%02x", nn.diff, nn.wc);

	resp[8] ^= 0x01;	/* corrupt CRC */
	CHECK(bm1720_parse_nonce(resp, sizeof(resp), &nn) == -1,
	      "parse_nonce: corrupt CRC accepted");
	resp[8] ^= 0x01;

	resp[8] |= 0x80;	/* flagged response skips the CRC check */
	CHECK(bm1720_parse_nonce(resp, sizeof(resp), &nn) == 1,
	      "parse_nonce: flagged frame not reported");
	resp[8] &= 0x7f;

	resp[0] = 0x55; resp[1] = 0xaa;		/* TX order = wrong for RX */
	CHECK(bm1720_parse_nonce(resp, sizeof(resp), &nn) == -1,
	      "parse_nonce: reversed header accepted");

	/* -- PLL table and helpers -------------------------------------- */

	for (i = 0; i < BM1720_FREQ_PLL_COUNT; i++) {
		const struct bm1720_freq_pll *e = &bm1720_freq_pll_table[i];
		unsigned fb = (e->vilpll >> 16) & 0xff;
		unsigned rd = (e->vilpll >> 8) & 0xff;
		unsigned p1 = (e->vilpll >> 4) & 0xf;
		unsigned p2 = e->vilpll & 0xf;

		CHECK(e->fildiv1 == ((fb << 12) | 0x40),
		      "table[%zu]: fildiv1", i);
		CHECK(e->fildiv2 == ((p1 << 8) | 0x20),
		      "table[%zu]: fildiv2", i);
		/* A3 envelope: refdiv always 2, postdiv1 1..7, postdiv2 1..2
		 * (the low-frequency rows use the doubled-fbdiv encoding). */
		CHECK(rd == 2 && (p2 == 1 || p2 == 2) && p1 >= 1 && p1 <= 7,
		      "table[%zu]: divider envelope", i);
		/* label is the (rounded) MHz the dividers produce off 25 MHz. */
		{
			double f = 25.0 * fb / (rd * p1 * p2);
			int lbl = atoi(e->freq);
			double d = f - lbl;
			CHECK(d < 1.0 && d > -1.0, "table[%zu]: freq label", i);
		}
	}

	CHECK(bm1720_lookup_pll(500, &v, NULL, NULL) >= 0 && v == 0x500221,
	      "lookup_pll(500)");
	CHECK(bm1720_lookup_pll(499, NULL, NULL, NULL) == -1,
	      "lookup_pll(499) should miss");
	CHECK(bm1720_compute_pll(500, &v, NULL, NULL) == 0 && v == 0x500221,
	      "compute_pll(500) = 0x%06x, want 0x500221", v);
	CHECK(bm1720_compute_pll(10, &v, NULL, NULL) == -1,
	      "compute_pll(10) should refuse");

	CHECK(bm1720_crc16((const uint8_t *)"123456789", 9) == 0x29b1,
	      "crc16 check vector");

	/* -- work/job frame (A3 send-work @0x41c90) --------------------- */
	{
		uint8_t header[BM1720_JOB_HEADER_LEN];
		uint8_t job[BM1720_JOB_FRAME_LEN];
		uint16_t jcrc;
		int rc, k;

		for (k = 0; k < BM1720_JOB_HEADER_LEN; k++)
			header[k] = (uint8_t)k;

		rc = bm1720_build_job(job, sizeof(job), header, 0x93);
		CHECK(rc == BM1720_JOB_FRAME_LEN, "build_job: len %d", rc);
		CHECK(job[0] == BM1720_JOB_TYPE, "build_job: type %02x", job[0]);
		/* work id is masked to 7 bits: 0x93 & 0x7f = 0x13 */
		CHECK(job[1] == 0x13, "build_job: work_id %02x", job[1]);
		/* first header word {00,01,02,03} ships big-endian {03,02,01,00} */
		CHECK(job[2] == 0x03 && job[3] == 0x02 &&
		      job[4] == 0x01 && job[5] == 0x00,
		      "build_job: header word 0 byteswap");
		/* last header word {4c,4d,4e,4f} -> {4f,4e,4d,4c} at buf[78..81] */
		CHECK(job[78] == 0x4f && job[79] == 0x4e &&
		      job[80] == 0x4d && job[81] == 0x4c,
		      "build_job: header word 19 byteswap");
		/* CRC16 covers buf[0..81] (type + id + 80 header bytes), stored
		 * high byte first at buf[82..83] */
		jcrc = bm1720_crc16(job, 2 + BM1720_JOB_HEADER_LEN);
		CHECK(job[82] == (uint8_t)(jcrc >> 8) && job[83] == (uint8_t)jcrc,
		      "build_job: crc16 %04x", jcrc);
	}

	if (failures) {
		printf("%d FAILURE(S)\n", failures);
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
