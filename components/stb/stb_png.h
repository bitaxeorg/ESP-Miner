#ifndef STB_PNG_H
#define STB_PNG_H

#include <stdint.h>
#include <stddef.h>

/**
 * Encode a 1-bit monochrome image to PNG
 * @param mono        Input image data (1 bit per pixel, packed in bytes)
 * @param w           Image width
 * @param h           Image height  
 * @param out         Output buffer (must be large enough for the PNG)
 * @param out_size    Final size of the output PNG
 * @return            0 on success, non-zero on error
 */
int stbi_write_png_monochrome_to_mem(const uint8_t* mono, int w, int h,
                                     uint8_t* out, int* out_size);

#endif // STB_PNG_H
