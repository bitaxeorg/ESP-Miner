/* bm1720.c - driver core for the Bitmain BM1720 mining ASIC.
 *
 * Reconstructed from shipped firmware. See VALIDATION.md for provenance and
 * for the confirmed/inferred split. The PLL table and its encoding are lifted
 * byte-exact from the firmware; the command framing, opcodes and CRC5 were
 * verified against the disassembly of both the AntRouter R3 (--sia) and
 * Antminer A3 binaries. Not yet validated against real silicon.
 */

#include <string.h>

#include "bm1720.h"
#include "bm1720_pll_table.h"

/* ------------------------------------------------------------------ *
 * CRC
 * ------------------------------------------------------------------ */

uint8_t bm1720_crc5(const uint8_t *data, size_t bit_len)
{
	uint8_t state[5] = { 1, 1, 1, 1, 1 };
	size_t i;

	for (i = 0; i < bit_len; i++) {
		uint8_t din = (data[i / 8] >> (7 - (i % 8))) & 1;
		uint8_t feedback = din ^ state[4];

		state[4] = state[3];
		state[3] = state[2];
		state[2] = state[1] ^ feedback;
		state[1] = state[0];
		state[0] = feedback;
	}

	/* state[4] is the MSB of the result. Both firmwares pack it this
	 * way (R3 @0x44f028, A3 @0x59464); the reverse ordering produces
	 * CRCs the chip rejects. */
	return (uint8_t)((state[4] << 4) | (state[3] << 3) | (state[2] << 2) |
			 (state[1] << 1) | state[0]);
}

uint16_t bm1720_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xffff;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (bit = 0; bit < 8; bit++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
					     : (uint16_t)(crc << 1);
	}

	return crc;
}

/* ------------------------------------------------------------------ *
 * Command layer
 * ------------------------------------------------------------------ */

int bm1720_build_vil(uint8_t *buf, size_t buf_len, uint8_t cmd, bool all,
		     uint8_t chip, uint8_t reg,
		     const uint8_t *payload, size_t payload_len)
{
	size_t frame_len = 4 + payload_len + 1;

	if (!buf || frame_len > buf_len || frame_len > 0xff)
		return -1;
	if (payload_len && !payload)
		return -1;

	buf[0] = (uint8_t)(BM1720_VIL_TYPE | (all ? BM1720_VIL_ALL : 0) |
			   (cmd & 0x0f));
	buf[1] = (uint8_t)frame_len;
	buf[2] = all ? 0 : chip;
	buf[3] = reg;
	if (payload_len)
		memcpy(buf + 4, payload, payload_len);

	/* The CRC covers every byte before itself, counted in bits. */
	buf[frame_len - 1] = bm1720_crc5(buf, (frame_len - 1) * 8);

	return (int)frame_len;
}

int bm1720_send_vil(struct bm1720_chain *chain, uint8_t cmd, bool all,
		    uint8_t chip, uint8_t reg,
		    const uint8_t *payload, size_t payload_len)
{
	uint8_t buf[2 + 16];
	uint8_t *frame = buf + 2;
	int len;

	if (!chain || !chain->tp.write)
		return -1;

	len = bm1720_build_vil(frame, sizeof(buf) - 2, cmd, all, chip, reg,
			       payload, payload_len);
	if (len < 0)
		return -1;

	if (chain->uart_preamble) {
		buf[0] = BM1720_PREAMBLE_0;
		buf[1] = BM1720_PREAMBLE_1;
		return chain->tp.write(chain->tp.ctx, buf, (size_t)len + 2);
	}

	return chain->tp.write(chain->tp.ctx, frame, (size_t)len);
}

int bm1720_chain_inactive(struct bm1720_chain *chain)
{
	return bm1720_send_vil(chain, BM1720_CMD_CHAIN_INACTIVE, true, 0, 0,
			       NULL, 0);
}

int bm1720_set_address(struct bm1720_chain *chain, uint8_t chip_addr)
{
	return bm1720_send_vil(chain, BM1720_CMD_SET_ADDRESS, false, chip_addr,
			       0, NULL, 0);
}

int bm1720_enumerate(struct bm1720_chain *chain)
{
	int i;

	if (!chain || chain->chip_count <= 0 || chain->chip_count > 256)
		return -1;
	if (chain->addr_interval <= 0)
		chain->addr_interval = 256 / chain->chip_count;
	if (chain->chip_count * chain->addr_interval > 256)
		return -1;

	if (bm1720_chain_inactive(chain) < 0)
		return -1;

	for (i = 0; i < chain->chip_count; i++) {
		uint8_t addr = (uint8_t)(i * chain->addr_interval);

		if (bm1720_set_address(chain, addr) < 0)
			return -1;
	}

	return chain->chip_count;
}

/* ------------------------------------------------------------------ *
 * PLL
 * ------------------------------------------------------------------ */

static int parse_freq_label(const char *s)
{
	int v = 0;

	for (; *s && *s != '.'; s++) {
		if (*s < '0' || *s > '9')
			return -1;
		v = v * 10 + (*s - '0');
	}

	return v;
}

int bm1720_lookup_pll(int freq_mhz, uint32_t *vilpll, uint32_t *fildiv1,
		      uint32_t *fildiv2)
{
	size_t i;

	for (i = 0; i < BM1720_FREQ_PLL_COUNT; i++) {
		if (parse_freq_label(bm1720_freq_pll_table[i].freq) != freq_mhz)
			continue;

		if (vilpll)
			*vilpll = bm1720_freq_pll_table[i].vilpll;
		if (fildiv1)
			*fildiv1 = bm1720_freq_pll_table[i].fildiv1;
		if (fildiv2)
			*fildiv2 = bm1720_freq_pll_table[i].fildiv2;

		return (int)i;
	}

	return -1;
}

int bm1720_compute_pll(int freq_mhz, uint32_t *vilpll, uint32_t *fildiv1,
		       uint32_t *fildiv2)
{
	/* Search only the divider space the shipped table actually uses:
	 * refdiv is 2 and postdiv2 is 1 in every one of the 99 entries, and
	 * postdiv1 is 2, 3 or 4. Pick the combination landing closest to
	 * the request; ties keep the first hit, i.e. the smallest postdiv1
	 * and fbdiv and with them the lowest VCO frequency. Combinations
	 * outside this envelope have never been seen programmed into the
	 * chip, so they are not offered here. */
	int best_fb = 0, best_p1 = 0;
	int best_err = 1 << 30;
	const int refdiv = 2, p2 = 1;
	int p1, fb;

	if (freq_mhz <= 0)
		return -1;

	for (p1 = 2; p1 <= 4; p1++) {
		for (fb = 32; fb <= 128; fb++) {
			/* freq = REF * fb / (refdiv * p1 * p2), scaled
			 * by 100 to compare without floating point. */
			int scaled = (BM1720_REF_CLK_MHZ * fb * 100) /
				     (refdiv * p1 * p2);
			int err = scaled - freq_mhz * 100;

			if (err < 0)
				err = -err;
			if (err < best_err) {
				best_err = err;
				best_fb = fb;
				best_p1 = p1;
			}
		}
	}

	/* Refuse requests that miss by more than ~2%: the caller asked for
	 * something the divider space cannot approximate. */
	if (!best_fb || best_err > freq_mhz * 2)
		return -1;

	if (vilpll)
		*vilpll = ((uint32_t)best_fb << 16) | ((uint32_t)refdiv << 8) |
			  ((uint32_t)best_p1 << 4) | (uint32_t)p2;
	if (fildiv1)
		*fildiv1 = ((uint32_t)best_fb << 12) | 0x40;
	if (fildiv2)
		*fildiv2 = ((uint32_t)best_p1 << 8) | 0x20;

	return 0;
}

int bm1720_set_frequency(struct bm1720_chain *chain, int freq_mhz)
{
	uint32_t vilpll = 0;
	uint8_t payload[4];
	int rc;

	if (!chain)
		return -1;

	if (bm1720_lookup_pll(freq_mhz, &vilpll, NULL, NULL) < 0 &&
	    bm1720_compute_pll(freq_mhz, &vilpll, NULL, NULL) < 0)
		return -1;

	payload[0] = (uint8_t)(vilpll >> 24);
	payload[1] = (uint8_t)(vilpll >> 16);
	payload[2] = (uint8_t)(vilpll >> 8);
	payload[3] = (uint8_t)vilpll;

	rc = bm1720_send_vil(chain, BM1720_CMD_WRITE_REG, true, 0,
			     BM1720_REG_PLL_PARAM, payload, sizeof(payload));
	if (rc < 0)
		return rc;

	chain->freq_mhz = freq_mhz;

	return 0;
}

int bm1720_set_ticket_mask(struct bm1720_chain *chain, uint32_t mask)
{
	uint8_t payload[4];

	payload[0] = (uint8_t)(mask >> 24);
	payload[1] = (uint8_t)(mask >> 16);
	payload[2] = (uint8_t)(mask >> 8);
	payload[3] = (uint8_t)mask;

	return bm1720_send_vil(chain, BM1720_CMD_WRITE_REG, true, 0,
			       BM1720_REG_TICKET_MASK, payload,
			       sizeof(payload));
}

int bm1720_read_register(struct bm1720_chain *chain, uint8_t chip, uint8_t reg,
			 uint8_t *buf, int timeout_ms)
{
	int rc;

	if (!chain || !chain->tp.read || !buf)
		return -1;

	rc = bm1720_send_vil(chain, BM1720_CMD_READ_REG, false, chip, reg,
			     NULL, 0);
	if (rc < 0)
		return rc;

	return chain->tp.read(chain->tp.ctx, buf, BM1720_RESP_LEN,
			      timeout_ms);
}

/* ------------------------------------------------------------------ *
 * Response / nonce return
 * ------------------------------------------------------------------ */

int bm1720_parse_nonce(const uint8_t *buf, size_t len, struct bm1720_nonce *out)
{
	if (!buf || !out || len < BM1720_RESP_LEN)
		return -1;

	/* The firmware rejects responses whose first two bytes are not the
	 * 0x55 0xAA preamble before doing anything else with them. */
	if (buf[0] != BM1720_PREAMBLE_0 || buf[1] != BM1720_PREAMBLE_1)
		return -1;

	/* Flagged responses are routed separately by the firmware without
	 * a CRC check. */
	if (buf[8] & 0x80) {
		out->flags = (uint8_t)(buf[8] >> 5);
		return 1;
	}

	/* CRC5 over the first 51 bits of the 7-byte body, preamble
	 * excluded, compared against the low 5 bits of the final byte. */
	if (bm1720_crc5(buf + 2, 51) != (buf[8] & 0x1f))
		return -1;

	out->nonce = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) |
		     ((uint32_t)buf[4] << 8) | (uint32_t)buf[5];
	out->chip = buf[6];
	out->reg = buf[7];
	out->flags = (uint8_t)(buf[8] >> 5);

	return 0;
}
