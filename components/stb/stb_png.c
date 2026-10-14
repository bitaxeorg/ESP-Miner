#include "stb_png.h"
#include "esp_rom_crc.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// PNG signature
static const uint8_t PNG_SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};

static void write_chunk(uint8_t* out, int* pos, const char* type, const uint8_t* data, size_t len) {
    uint8_t* p = out + *pos;
    
    // Length (Big Endian)
    p[0] = (len >> 24) & 0xFF;
    p[1] = (len >> 16) & 0xFF;
    p[2] = (len >> 8) & 0xFF;
    p[3] = len & 0xFF;
    
    // Type
    memcpy(p + 4, type, 4);
    
    // Data
    if (data && len > 0) {
        memcpy(p + 8, data, len);
    }
    
    // CRC (Type + Data)
    // ESP-IDF ROM CRC handles the initial and final XORs if used with 0 as initial seed.
    uint32_t crc = esp_rom_crc32_le(0, (const uint8_t*)p + 4, 4);
    if (data && len > 0) {
        crc = esp_rom_crc32_le(crc, data, len);
    }
    
    p[8 + len + 0] = (crc >> 24) & 0xFF;
    p[8 + len + 1] = (crc >> 16) & 0xFF;
    p[8 + len + 2] = (crc >> 8) & 0xFF;
    p[8 + len + 3] = crc & 0xFF;
    
    *pos += 4 + 4 + len + 4;
}

int stbi_write_png_monochrome_to_mem(const uint8_t* mono, int w, int h, uint8_t* out, int* out_size) {
    if (!mono || w <= 0 || h <= 0 || !out || !out_size) {
        return -1;
    }
    
    int pos = 0;
    
    // PNG signature
    memcpy(out, PNG_SIGNATURE, 8);
    pos = 8;
    
    // IHDR for 1-bit grayscale
    uint8_t ihdr[13];
    ihdr[0] = (w >> 24) & 0xFF;
    ihdr[1] = (w >> 16) & 0xFF;
    ihdr[2] = (w >> 8) & 0xFF;
    ihdr[3] = w & 0xFF;
    ihdr[4] = (h >> 24) & 0xFF;
    ihdr[5] = (h >> 16) & 0xFF;
    ihdr[6] = (h >> 8) & 0xFF;
    ihdr[7] = h & 0xFF;
    ihdr[8] = 1;  // bit depth: 1
    ihdr[9] = 0;  // color type: 0 (grayscale)
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    
    write_chunk(out, &pos, "IHDR", ihdr, 13);
    
    // Calculate stride and raw data size
    int stride = (w + 7) / 8;
    size_t row_size = 1 + stride;
    size_t raw_len = row_size * h;
    
    // Prepare IDAT data (zlib stored blocks)
    // For small images like 128x32, this always fits in one 65535 block.
    size_t zlib_overhead = 2 + 5 + 4; // Header + Block Header + Adler32
    size_t idat_len = raw_len + zlib_overhead;
    
    // We assemble the IDAT data in-place in a stack buffer for simplicity
    uint8_t idat_buf[2048];
    if (idat_len > sizeof(idat_buf)) return -1;
    
    uint32_t idat_pos = 0;
    
    // zlib header (no compression)
    idat_buf[idat_pos++] = 0x78; // CMF
    idat_buf[idat_pos++] = 0x01; // FLG
    
    // Stored block header
    idat_buf[idat_pos++] = 1; // BFINAL=1, BTYPE=00
    idat_buf[idat_pos++] = raw_len & 0xFF;
    idat_buf[idat_pos++] = (raw_len >> 8) & 0xFF;
    idat_buf[idat_pos++] = (~raw_len) & 0xFF;
    idat_buf[idat_pos++] = ((~raw_len) >> 8) & 0xFF;
    
    // Copy image data row by row and add filter bytes
    uint32_t a = 1, b = 0; // Adler32
    for (int y = 0; y < h; y++) {
        // Filter byte: none (0)
        uint8_t filter = 0;
        idat_buf[idat_pos++] = filter;
        
        // Adler32 update for filter byte
        a = (a + filter) % 65521;
        b = (b + a) % 65521;
        
        // Image data
        const uint8_t* src_row = mono + y * stride;
        memcpy(idat_buf + idat_pos, src_row, stride);
        
        // Adler32 update for image data
        for (int x = 0; x < stride; x++) {
            a = (a + src_row[x]) % 65521;
            b = (b + a) % 65521;
        }
        idat_pos += stride;
    }
    
    // Adler32 checksum
    uint32_t adler = (b << 16) | a;
    idat_buf[idat_pos++] = (adler >> 24) & 0xFF;
    idat_buf[idat_pos++] = (adler >> 16) & 0xFF;
    idat_buf[idat_pos++] = (adler >> 8) & 0xFF;
    idat_buf[idat_pos++] = adler & 0xFF;
    
    write_chunk(out, &pos, "IDAT", idat_buf, idat_pos);
    
    // IEND
    write_chunk(out, &pos, "IEND", NULL, 0);
    
    *out_size = pos;
    return 0;
}
