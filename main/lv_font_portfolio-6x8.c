/*******************************************************************************
 * Size: 8 px
 * Bpp: 1
 * Opts: --font oldschool_pc_font_pack_v2.2_linux/ttf - Mx (mixed outline+bitmap)/Mx437_Portfolio_6x8.ttf --bpp 1 --size 8 --format lvgl --range 0x20-0x7F -o portfolio_6x8
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef PORTFOLIO_6X8
#define PORTFOLIO_6X8 1
#endif

#if PORTFOLIO_6X8

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

    /* U+0021 "!" */
    0x20, 0x82, 0x8, 0x20, 0x2, 0x0,

    /* U+0022 "\"" */
    0x51, 0x45, 0x0, 0x0, 0x0, 0x0,

    /* U+0023 "#" */
    0x51, 0x4f, 0x94, 0xf9, 0x45, 0x0,

    /* U+0024 "$" */
    0x21, 0xe8, 0x1c, 0xb, 0xc2, 0x0,

    /* U+0025 "%" */
    0xc3, 0x21, 0x8, 0x42, 0x61, 0x80,

    /* U+0026 "&" */
    0x62, 0x4a, 0x10, 0xaa, 0x46, 0x80,

    /* U+0027 "'" */
    0x30, 0x42, 0x0, 0x0, 0x0, 0x0,

    /* U+0028 "(" */
    0x10, 0x84, 0x10, 0x40, 0x81, 0x0,

    /* U+0029 ")" */
    0x40, 0x81, 0x4, 0x10, 0x84, 0x0,

    /* U+002A "*" */
    0x0, 0x8a, 0x9c, 0xa8, 0x80, 0x0,

    /* U+002B "+" */
    0x0, 0x82, 0x3e, 0x20, 0x80, 0x0,

    /* U+002C "," */
    0x0, 0x0, 0x0, 0x30, 0x42, 0x0,

    /* U+002D "-" */
    0x0, 0x0, 0x3e, 0x0, 0x0, 0x0,

    /* U+002E "." */
    0x0, 0x0, 0x0, 0x0, 0xc3, 0x0,

    /* U+002F "/" */
    0x0, 0x21, 0x8, 0x42, 0x0, 0x0,

    /* U+0030 "0" */
    0x72, 0x29, 0xaa, 0xca, 0x27, 0x0,

    /* U+0031 "1" */
    0x21, 0x82, 0x8, 0x20, 0x87, 0x0,

    /* U+0032 "2" */
    0x72, 0x20, 0x84, 0x21, 0xf, 0x80,

    /* U+0033 "3" */
    0xf8, 0x42, 0x4, 0xa, 0x27, 0x0,

    /* U+0034 "4" */
    0x10, 0xc5, 0x24, 0xf8, 0x41, 0x0,

    /* U+0035 "5" */
    0xfa, 0xf, 0x2, 0xa, 0x27, 0x0,

    /* U+0036 "6" */
    0x31, 0x8, 0x3c, 0x8a, 0x27, 0x0,

    /* U+0037 "7" */
    0xf8, 0x21, 0x8, 0x20, 0x82, 0x0,

    /* U+0038 "8" */
    0x72, 0x28, 0x9c, 0x8a, 0x27, 0x0,

    /* U+0039 "9" */
    0x72, 0x28, 0x9e, 0x8, 0x46, 0x0,

    /* U+003A ":" */
    0x0, 0xc3, 0x0, 0x30, 0xc0, 0x0,

    /* U+003B ";" */
    0x0, 0xc3, 0x0, 0x30, 0x42, 0x0,

    /* U+003C "<" */
    0x8, 0x42, 0x10, 0x20, 0x40, 0x80,

    /* U+003D "=" */
    0x0, 0xf, 0x80, 0xf8, 0x0, 0x0,

    /* U+003E ">" */
    0x81, 0x2, 0x4, 0x21, 0x8, 0x0,

    /* U+003F "?" */
    0x72, 0x20, 0x84, 0x20, 0x2, 0x0,

    /* U+0040 "@" */
    0x72, 0x29, 0xaa, 0x92, 0x7, 0x0,

    /* U+0041 "A" */
    0x72, 0x28, 0xa2, 0xfa, 0x28, 0x80,

    /* U+0042 "B" */
    0xf2, 0x28, 0xbc, 0x8a, 0x2f, 0x0,

    /* U+0043 "C" */
    0x72, 0x28, 0x20, 0x82, 0x27, 0x0,

    /* U+0044 "D" */
    0xe2, 0x48, 0xa2, 0x8a, 0x4e, 0x0,

    /* U+0045 "E" */
    0xfa, 0x8, 0x3c, 0x82, 0xf, 0x80,

    /* U+0046 "F" */
    0xfa, 0x8, 0x3c, 0x82, 0x8, 0x0,

    /* U+0047 "G" */
    0x72, 0x28, 0x20, 0xba, 0x27, 0x80,

    /* U+0048 "H" */
    0x8a, 0x28, 0xbe, 0x8a, 0x28, 0x80,

    /* U+0049 "I" */
    0x70, 0x82, 0x8, 0x20, 0x87, 0x0,

    /* U+004A "J" */
    0x38, 0x41, 0x4, 0x12, 0x46, 0x0,

    /* U+004B "K" */
    0x8a, 0x4a, 0x30, 0xa2, 0x48, 0x80,

    /* U+004C "L" */
    0x82, 0x8, 0x20, 0x82, 0xf, 0x80,

    /* U+004D "M" */
    0x8b, 0x6a, 0xaa, 0x8a, 0x28, 0x80,

    /* U+004E "N" */
    0x8a, 0x2c, 0xaa, 0x9a, 0x28, 0x80,

    /* U+004F "O" */
    0x72, 0x28, 0xa2, 0x8a, 0x27, 0x0,

    /* U+0050 "P" */
    0xf2, 0x28, 0xbc, 0x82, 0x8, 0x0,

    /* U+0051 "Q" */
    0x72, 0x28, 0xa2, 0xaa, 0x46, 0x80,

    /* U+0052 "R" */
    0xf2, 0x28, 0xbc, 0xa2, 0x48, 0x80,

    /* U+0053 "S" */
    0x72, 0x28, 0x1c, 0xa, 0x27, 0x0,

    /* U+0054 "T" */
    0xf8, 0x82, 0x8, 0x20, 0x82, 0x0,

    /* U+0055 "U" */
    0x8a, 0x28, 0xa2, 0x8a, 0x27, 0x0,

    /* U+0056 "V" */
    0x8a, 0x28, 0xa2, 0x89, 0x42, 0x0,

    /* U+0057 "W" */
    0x8a, 0x28, 0xaa, 0xab, 0x68, 0x80,

    /* U+0058 "X" */
    0x8a, 0x25, 0x8, 0x52, 0x28, 0x80,

    /* U+0059 "Y" */
    0x8a, 0x28, 0x9c, 0x20, 0x82, 0x0,

    /* U+005A "Z" */
    0xf8, 0x21, 0x8, 0x42, 0xf, 0x80,

    /* U+005B "[" */
    0x71, 0x4, 0x10, 0x41, 0x7, 0x0,

    /* U+005C "\\" */
    0x2, 0x4, 0x8, 0x10, 0x20, 0x0,

    /* U+005D "]" */
    0x70, 0x41, 0x4, 0x10, 0x47, 0x0,

    /* U+005E "^" */
    0x21, 0x48, 0x80, 0x0, 0x0, 0x0,

    /* U+005F "_" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x3f,

    /* U+0060 "`" */
    0x61, 0x2, 0x0, 0x0, 0x0, 0x0,

    /* U+0061 "a" */
    0x0, 0x7, 0x2, 0x7a, 0x27, 0x80,

    /* U+0062 "b" */
    0x82, 0xb, 0x32, 0x8a, 0x2f, 0x0,

    /* U+0063 "c" */
    0x0, 0x7, 0x20, 0x82, 0x27, 0x0,

    /* U+0064 "d" */
    0x8, 0x26, 0xa6, 0x8a, 0x27, 0x80,

    /* U+0065 "e" */
    0x0, 0x7, 0x22, 0xfa, 0x7, 0x80,

    /* U+0066 "f" */
    0x31, 0x24, 0x3c, 0x41, 0x4, 0x0,

    /* U+0067 "g" */
    0x0, 0x7, 0xa2, 0x78, 0x27, 0x0,

    /* U+0068 "h" */
    0x82, 0xb, 0x32, 0x8a, 0x28, 0x80,

    /* U+0069 "i" */
    0x20, 0x6, 0x8, 0x20, 0x87, 0x0,

    /* U+006A "j" */
    0x10, 0x3, 0x4, 0x12, 0x46, 0x0,

    /* U+006B "k" */
    0x82, 0x8, 0xa4, 0xa3, 0x48, 0x80,

    /* U+006C "l" */
    0x60, 0x82, 0x8, 0x20, 0x87, 0x0,

    /* U+006D "m" */
    0x0, 0xd, 0x2a, 0xaa, 0x28, 0x80,

    /* U+006E "n" */
    0x0, 0xb, 0x32, 0x8a, 0x28, 0x80,

    /* U+006F "o" */
    0x0, 0x7, 0x22, 0x8a, 0x27, 0x0,

    /* U+0070 "p" */
    0x0, 0xf, 0x22, 0xf2, 0x8, 0x0,

    /* U+0071 "q" */
    0x0, 0x7, 0xa2, 0x78, 0x20, 0x80,

    /* U+0072 "r" */
    0x0, 0xb, 0x32, 0x82, 0x8, 0x0,

    /* U+0073 "s" */
    0x0, 0x7, 0xa0, 0x70, 0x2f, 0x0,

    /* U+0074 "t" */
    0x41, 0xf, 0x10, 0x41, 0x23, 0x0,

    /* U+0075 "u" */
    0x0, 0x8, 0xa2, 0x8a, 0x66, 0x80,

    /* U+0076 "v" */
    0x0, 0x8, 0xa2, 0x89, 0x42, 0x0,

    /* U+0077 "w" */
    0x0, 0x8, 0xa2, 0xaa, 0xa5, 0x0,

    /* U+0078 "x" */
    0x0, 0x8, 0x94, 0x21, 0x48, 0x80,

    /* U+0079 "y" */
    0x0, 0x8, 0xa2, 0x78, 0x2f, 0x0,

    /* U+007A "z" */
    0x0, 0xf, 0x84, 0x21, 0xf, 0x80,

    /* U+007B "{" */
    0x18, 0x82, 0x18, 0x20, 0x81, 0x80,

    /* U+007C "|" */
    0x20, 0x82, 0x0, 0x20, 0x82, 0x0,

    /* U+007D "}" */
    0xc0, 0x82, 0xc, 0x20, 0x8c, 0x0,

    /* U+007E "~" */
    0x6a, 0xc0, 0x0, 0x0, 0x0, 0x0,

    /* U+007F "" */
    0x0, 0x2, 0x14, 0x8b, 0xe0, 0x0,

    // Extra characters

    /* U+00B0 "°" */
    0x10, 0xA1, 0x00, 0x00, 0x00, 0x00,

    // Unicode characters

    /* U+2191 "↑" */
    0x21, 0xCF, 0x88, 0x20, 0x82, 0x00,

    /* U+2193 "↓" */
    0x20, 0x82, 0x08, 0xF9, 0xC2, 0x00,
   
    /* U+2195 "↕" */
    0x21, 0xCF, 0x88, 0xF9, 0xC2, 0x00,
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 6, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 12, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 18, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 24, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 30, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 36, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 42, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 48, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 54, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 60, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 66, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 72, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 78, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 84, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 90, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 96, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 102, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 108, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 114, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 120, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 126, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 132, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 138, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 144, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 150, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 156, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 162, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 168, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 174, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 180, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 186, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 192, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 198, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 204, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 210, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 216, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 222, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 228, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 234, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 240, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 246, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 252, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 258, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 264, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 270, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 276, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 282, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 288, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 294, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 300, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 306, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 312, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 318, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 324, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 330, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 336, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 342, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 348, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 354, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 360, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 366, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 372, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 378, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 384, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 390, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 396, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 402, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 408, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 414, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 420, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 426, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 432, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 438, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 444, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 450, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 456, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 462, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 468, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 474, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 480, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 486, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 492, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 498, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 504, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 510, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 516, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 522, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 528, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 534, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 540, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 546, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 552, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 558, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 564, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 570, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},

    // Extra characters
    {.bitmap_index = 576, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},

    // Unicode characters
    {.bitmap_index = 582, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 588, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 594, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

#define EXTRA_CHARACTERS_RANGE_STATE 128

static const uint16_t extra_characters_list_1[] = {
    0x00B0 - EXTRA_CHARACTERS_RANGE_STATE,
};

#define UNICODE_RANGE_STATE 0x2191

static const uint16_t unicode_list_1[] = {
    0x2191 - UNICODE_RANGE_STATE,
    0x2193 - UNICODE_RANGE_STATE,
    0x2195 - UNICODE_RANGE_STATE,
};

static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 96, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = EXTRA_CHARACTERS_RANGE_STATE, .range_length = 128, .glyph_id_start = 97,
        .unicode_list = extra_characters_list_1, .glyph_id_ofs_list = NULL, .list_length = 1, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = UNICODE_RANGE_STATE, .range_length = 5, .glyph_id_start = 98,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 3,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_portfolio_6x8 = {
#else
lv_font_t lv_font_portfolio_6x8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 8,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if PORTFOLIO_6X8*/

