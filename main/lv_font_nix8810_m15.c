/*******************************************************************************
 * Size: 15 px
 * Bpp: 1
 * Opts: --font oldschool_pc_font_pack_v2.2_pack/ttf - Mx (mixed outline+bitmap)/Mx437_Nix8810_M15.ttf --bpp 1 --size 15 --format lvgl --range 0x20-0xFFFF -o lv_font_nix8810_m15.c
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



#ifndef NIX8810_M15
#define NIX8810_M15 1
#endif

#if NIX8810_M15

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x00,

    /* U+0021 "!" */
    0xff, 0xfc, 0x3c,

    /* U+0022 "\"" */
    0xcf, 0x3c, 0xc0,

    /* U+0023 "#" */
    0x44, 0x8d, 0xff, 0xec, 0x48, 0x91, 0xbf, 0xfd,
    0x89, 0x10,

    /* U+0024 "$" */
    0x10, 0xfb, 0xfe, 0xb7, 0x07, 0x07, 0x6b, 0xfe,
    0xf8, 0x40,

    /* U+0025 "%" */
    0x43, 0xce, 0x97, 0x64, 0x83, 0x0c, 0x12, 0x6e,
    0x97, 0x3c, 0x20,

    /* U+0026 "&" */
    0x38, 0xd9, 0xb1, 0xc3, 0x07, 0x1e, 0xe7, 0xcc,
    0xf8, 0xd8,

    /* U+0027 "'" */
    0x77, 0x6c,

    /* U+0028 "(" */
    0x19, 0xdc, 0xce, 0x73, 0x8c, 0x71, 0xc6,

    /* U+0029 ")" */
    0xc7, 0x1c, 0x63, 0x9c, 0xe6, 0x77, 0x30,

    /* U+002A "*" */
    0x6c, 0xd8, 0xe7, 0xff, 0xe7, 0x1b, 0x36,

    /* U+002B "+" */
    0x31, 0x8d, 0xff, 0x98, 0xc6,

    /* U+002C "," */
    0x77, 0x6c,

    /* U+002D "-" */
    0xff, 0xfc,

    /* U+002E "." */
    0xff, 0x80,

    /* U+002F "/" */
    0x0c, 0x31, 0x86, 0x30, 0xc6, 0x18, 0xc3, 0x0c,
    0x00,

    /* U+0030 "0" */
    0x7d, 0xff, 0x1e, 0x7d, 0xfe, 0xf9, 0xe3, 0xc7,
    0xfd, 0xf0,

    /* U+0031 "1" */
    0x11, 0x9d, 0xe3, 0x18, 0xc6, 0x37, 0xfe,

    /* U+0032 "2" */
    0x7d, 0xff, 0x1e, 0x30, 0xe3, 0x8e, 0x38, 0xe1,
    0xff, 0xf8,

    /* U+0033 "3" */
    0x7d, 0xff, 0x18, 0x33, 0xc7, 0x81, 0x83, 0xc7,
    0xfd, 0xf0,

    /* U+0034 "4" */
    0x04, 0x18, 0x71, 0xe6, 0xd9, 0xbf, 0xff, 0x0c,
    0x3c, 0x78,

    /* U+0035 "5" */
    0xff, 0xff, 0x06, 0x0f, 0xdf, 0xc1, 0x83, 0xc7,
    0xfd, 0xf0,

    /* U+0036 "6" */
    0x38, 0xe3, 0x86, 0x0f, 0xdf, 0xf1, 0xe3, 0xc7,
    0xfd, 0xf0,

    /* U+0037 "7" */
    0xff, 0xff, 0x18, 0x30, 0xe1, 0x87, 0x0c, 0x18,
    0x30, 0x60,

    /* U+0038 "8" */
    0x7d, 0xff, 0x1e, 0x37, 0xcf, 0xb1, 0xe3, 0xc7,
    0xfd, 0xf0,

    /* U+0039 "9" */
    0x7d, 0xff, 0x1e, 0x3c, 0x7f, 0xdf, 0x83, 0x06,
    0xfd, 0xf0,

    /* U+003A ":" */
    0xfc, 0x0f, 0xc0,

    /* U+003B ";" */
    0xfc, 0x3f, 0x60,

    /* U+003C "<" */
    0x06, 0x18, 0x61, 0x86, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18,

    /* U+003D "=" */
    0xff, 0xfc, 0x00, 0x0f, 0xff, 0xc0,

    /* U+003E ">" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc3, 0x0c, 0x30,
    0xc3, 0x00,

    /* U+003F "?" */
    0x7b, 0xfc, 0xf3, 0x0c, 0x73, 0x8c, 0x00, 0xc3,
    0x00,

    /* U+0040 "@" */
    0x7d, 0xff, 0x1e, 0xfd, 0xfb, 0xf7, 0xef, 0xc1,
    0xe1, 0xc0,

    /* U+0041 "A" */
    0x38, 0xfb, 0xbe, 0x3c, 0x78, 0xff, 0xff, 0xc7,
    0x8f, 0x18,

    /* U+0042 "B" */
    0xfb, 0xfc, 0xf3, 0xfb, 0xec, 0xf3, 0xcf, 0xff,
    0x80,

    /* U+0043 "C" */
    0x7d, 0xff, 0x1e, 0x3c, 0x18, 0x30, 0x63, 0xc7,
    0xfd, 0xf0,

    /* U+0044 "D" */
    0xf9, 0xf9, 0xbb, 0x36, 0x6c, 0xd9, 0xb3, 0x6f,
    0xfb, 0xe0,

    /* U+0045 "E" */
    0xff, 0xfd, 0x9b, 0x07, 0x8f, 0x18, 0x30, 0x67,
    0xff, 0xf8,

    /* U+0046 "F" */
    0xff, 0xfd, 0x9b, 0x07, 0x8f, 0x18, 0x30, 0x61,
    0xe3, 0xc0,

    /* U+0047 "G" */
    0x3c, 0xff, 0x9e, 0x0c, 0x19, 0xf1, 0xe3, 0xe6,
    0xfc, 0xf8,

    /* U+0048 "H" */
    0xc7, 0x8f, 0x1e, 0x3f, 0xff, 0xf1, 0xe3, 0xc7,
    0x8f, 0x18,

    /* U+0049 "I" */
    0xff, 0x66, 0x66, 0x66, 0x6f, 0xf0,

    /* U+004A "J" */
    0x1e, 0x3c, 0x30, 0x60, 0xc1, 0x83, 0x66, 0xcd,
    0xf9, 0xe0,

    /* U+004B "K" */
    0xc7, 0x9f, 0x77, 0xcf, 0x1c, 0x3c, 0x7c, 0xdd,
    0x9f, 0x18,

    /* U+004C "L" */
    0xf1, 0xe1, 0x83, 0x06, 0x0c, 0x18, 0x31, 0x67,
    0xff, 0xf8,

    /* U+004D "M" */
    0x83, 0x8f, 0x1f, 0x7f, 0xff, 0xf5, 0xe3, 0xc7,
    0x8f, 0x18,

    /* U+004E "N" */
    0xc7, 0x8f, 0x9f, 0x3f, 0x7f, 0xff, 0xef, 0xcf,
    0x9f, 0x18,

    /* U+004F "O" */
    0x38, 0xfb, 0xbe, 0x3c, 0x78, 0xf1, 0xe3, 0xee,
    0xf8, 0xe0,

    /* U+0050 "P" */
    0xfd, 0xff, 0x1e, 0x3f, 0xff, 0xb0, 0x60, 0xc1,
    0x83, 0x00,

    /* U+0051 "Q" */
    0x38, 0xfb, 0xbe, 0x3c, 0x78, 0xf1, 0xf3, 0xfe,
    0xf8, 0xe0, 0xe0,

    /* U+0052 "R" */
    0xfd, 0xff, 0x1e, 0x3f, 0xff, 0xbc, 0x7c, 0xdd,
    0x9f, 0x18,

    /* U+0053 "S" */
    0x7d, 0xff, 0x1e, 0x0f, 0x0f, 0x87, 0x83, 0xc7,
    0xfd, 0xf0,

    /* U+0054 "T" */
    0xff, 0xfb, 0x4c, 0x30, 0xc3, 0x0c, 0x31, 0xe7,
    0x80,

    /* U+0055 "U" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0xe3, 0xc7,
    0xfd, 0xf0,

    /* U+0056 "V" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0xf7, 0x7c,
    0x70, 0x40,

    /* U+0057 "W" */
    0xc7, 0x8f, 0x1e, 0xbd, 0x7a, 0xf5, 0xff, 0xfe,
    0xf9, 0xb0,

    /* U+0058 "X" */
    0xc7, 0x8f, 0xbb, 0x67, 0xc7, 0x1f, 0x36, 0xef,
    0x8f, 0x18,

    /* U+0059 "Y" */
    0xcf, 0x3c, 0xf3, 0xcf, 0xf7, 0x8c, 0x31, 0xe7,
    0x80,

    /* U+005A "Z" */
    0xff, 0xfc, 0x18, 0x71, 0xc7, 0x1c, 0x70, 0xc1,
    0xff, 0xf8,

    /* U+005B "[" */
    0xfc, 0xcc, 0xcc, 0xcc, 0xcc, 0xf0,

    /* U+005C "\\" */
    0xc3, 0x06, 0x18, 0x30, 0xc1, 0x86, 0x0c, 0x30,
    0xc0,

    /* U+005D "]" */
    0xf3, 0x33, 0x33, 0x33, 0x33, 0xf0,

    /* U+005E "^" */
    0x23, 0xb6,

    /* U+005F "_" */
    0xff, 0xff,

    /* U+0060 "`" */
    0xd9, 0x10,

    /* U+0061 "a" */
    0x78, 0xf8, 0x33, 0xef, 0xd9, 0xbf, 0xbb,

    /* U+0062 "b" */
    0xc3, 0x0c, 0x3c, 0xfb, 0x3c, 0xf3, 0xcf, 0xff,
    0x80,

    /* U+0063 "c" */
    0x7d, 0xff, 0x1e, 0x0c, 0x18, 0xff, 0xbe,

    /* U+0064 "d" */
    0x1c, 0x18, 0x31, 0xe7, 0xd9, 0xb3, 0x66, 0xcd,
    0xfd, 0xd8,

    /* U+0065 "e" */
    0x7d, 0xff, 0x1f, 0xff, 0xf8, 0x3f, 0xbe,

    /* U+0066 "f" */
    0x1c, 0x7c, 0xd9, 0x8f, 0xdf, 0x8c, 0x18, 0x30,
    0xf1, 0xe0,

    /* U+0067 "g" */
    0x7b, 0xff, 0x1e, 0x3f, 0xef, 0xc1, 0xbf, 0x7c,

    /* U+0068 "h" */
    0xe0, 0xc1, 0x83, 0xc7, 0xcc, 0xd9, 0xb3, 0x66,
    0xcf, 0x98,

    /* U+0069 "i" */
    0x66, 0x0e, 0xe6, 0x66, 0xff,

    /* U+006A "j" */
    0x0c, 0x30, 0x07, 0x1c, 0x30, 0xf3, 0xcf, 0xf7,
    0x80,

    /* U+006B "k" */
    0xc1, 0x83, 0x06, 0x7d, 0xdf, 0x3c, 0x78, 0xf9,
    0xbb, 0x38,

    /* U+006C "l" */
    0xee, 0x66, 0x66, 0x66, 0x6f, 0xf0,

    /* U+006D "m" */
    0xad, 0xff, 0xfe, 0xbd, 0x7a, 0xf5, 0xe3,

    /* U+006E "n" */
    0xdd, 0xfd, 0x9b, 0x36, 0x6c, 0xd9, 0xb3,

    /* U+006F "o" */
    0x7d, 0xff, 0x1e, 0x3c, 0x78, 0xff, 0xbe,

    /* U+0070 "p" */
    0xf9, 0xfd, 0x9b, 0x37, 0xef, 0x18, 0x78, 0xf0,

    /* U+0071 "q" */
    0x77, 0xff, 0x36, 0x6f, 0xcf, 0x83, 0x0f, 0x1e,

    /* U+0072 "r" */
    0xdd, 0xfd, 0xdb, 0x36, 0x0c, 0x18, 0x30,

    /* U+0073 "s" */
    0x7d, 0xff, 0x8b, 0xc1, 0xd9, 0xff, 0xbe,

    /* U+0074 "t" */
    0x60, 0xc1, 0x87, 0xcf, 0x8c, 0x18, 0x33, 0x66,
    0xfc, 0xf0,

    /* U+0075 "u" */
    0xcd, 0x9b, 0x36, 0x6c, 0xd9, 0xbf, 0xbb,

    /* U+0076 "v" */
    0xc7, 0x8f, 0x1e, 0x3e, 0xef, 0x8e, 0x08,

    /* U+0077 "w" */
    0xc7, 0x8f, 0x5e, 0xbd, 0x7f, 0xff, 0xb6,

    /* U+0078 "x" */
    0xc7, 0xdd, 0xf1, 0xc3, 0x8f, 0xbb, 0xe3,

    /* U+0079 "y" */
    0xe7, 0xcd, 0x9b, 0x37, 0xe7, 0xc1, 0xbf, 0x7c,

    /* U+007A "z" */
    0xff, 0xfc, 0x38, 0xe3, 0x8e, 0x3f, 0xff,

    /* U+007B "{" */
    0x36, 0x66, 0xcc, 0x66, 0x66, 0x30,

    /* U+007C "|" */
    0xff, 0xcf, 0xfc,

    /* U+007D "}" */
    0xc6, 0x66, 0x33, 0x66, 0x66, 0xc0,

    /* U+007E "~" */
    0x47, 0x6e, 0x20,

    /* U+007F "" */
    0x10, 0x20, 0xe1, 0x46, 0xc8, 0xbf, 0x80,

    /* U+00A0 " " */
    0x00,

    /* U+00A1 "¡" */
    0xf0, 0xff, 0xfc,

    /* U+00A2 "¢" */
    0x18, 0x31, 0xf7, 0x7c, 0x18, 0x30, 0x77, 0x7c,
    0x30, 0x60,

    /* U+00A3 "£" */
    0x38, 0xf9, 0x93, 0x0f, 0x9f, 0x18, 0x70, 0xf3,
    0xff, 0x70,

    /* U+00A5 "¥" */
    0x87, 0x37, 0x8c, 0xff, 0xf3, 0x3f, 0xfc, 0xc3,
    0x00,

    /* U+00A7 "§" */
    0x77, 0xe1, 0xe7, 0xc7, 0xcf, 0x0f, 0xdc,

    /* U+00AA "ª" */
    0x78, 0x37, 0xff, 0xcf, 0xf7, 0x40, 0xfc,

    /* U+00AB "«" */
    0x36, 0xdb, 0x66, 0xc6, 0xc6, 0xc0,

    /* U+00AC "¬" */
    0xff, 0xfc, 0x18, 0x30, 0x60,

    /* U+00B0 "°" */
    0x31, 0x28, 0x61, 0x48, 0xc0,

    /* U+00B1 "±" */
    0x31, 0x8d, 0xff, 0x98, 0xc6, 0x07, 0xfe,

    /* U+00B2 "²" */
    0x74, 0xc6, 0x64, 0x7c,

    /* U+00B5 "µ" */
    0xcd, 0x9b, 0x36, 0x6c, 0xdf, 0xfd, 0xe0, 0xc0,

    /* U+00B6 "¶" */
    0x7f, 0xfd, 0x75, 0xd7, 0x5f, 0x5d, 0x14, 0x51,
    0x40,

    /* U+00B7 "·" */
    0xc0,

    /* U+00BA "º" */
    0x7d, 0xff, 0x1e, 0x3c, 0x7f, 0xdf, 0x00, 0xfe,

    /* U+00BB "»" */
    0xd8, 0xd8, 0xd9, 0xb6, 0xdb, 0x00,

    /* U+00BC "¼" */
    0x40, 0x85, 0x1a, 0x65, 0x86, 0x19, 0x66, 0x94,
    0x7c, 0x10,

    /* U+00BD "½" */
    0x40, 0x85, 0x1a, 0x65, 0x86, 0x1b, 0xe1, 0x9e,
    0x20, 0x78,

    /* U+00BF "¿" */
    0x30, 0x60, 0x01, 0x83, 0x0e, 0x38, 0x63, 0xee,
    0xf8, 0xe0,

    /* U+00C4 "Ä" */
    0xc6, 0x00, 0xe3, 0xec, 0x78, 0xff, 0xff, 0xc7,
    0x8f, 0x18,

    /* U+00C5 "Å" */
    0x38, 0x88, 0xe0, 0x07, 0xd8, 0xf1, 0xff, 0xc7,
    0x8f, 0x18,

    /* U+00C6 "Æ" */
    0x2e, 0xff, 0xe6, 0xcd, 0xff, 0xfe, 0x6c, 0xd9,
    0xbf, 0x78,

    /* U+00C7 "Ç" */
    0x7d, 0xff, 0x1e, 0x0c, 0x18, 0xff, 0xbe, 0x18,
    0x10, 0x60,

    /* U+00C9 "É" */
    0x18, 0x60, 0x07, 0xff, 0xec, 0x1f, 0x3e, 0x61,
    0xff, 0xf8,

    /* U+00D1 "Ñ" */
    0x73, 0x18, 0x06, 0x3e, 0x7e, 0xff, 0xef, 0xcf,
    0x8f, 0x18,

    /* U+00D6 "Ö" */
    0xc6, 0x00, 0xe3, 0xee, 0xf8, 0xf1, 0xe3, 0xee,
    0xf8, 0xe0,

    /* U+00DC "Ü" */
    0xc6, 0x03, 0x1e, 0x3c, 0x78, 0xf1, 0xe3, 0xc7,
    0xfd, 0xf0,

    /* U+00DF "ß" */
    0x1c, 0x7d, 0x9b, 0x37, 0xcc, 0xd9, 0xb7, 0x6d,
    0xc3, 0x00,

    /* U+00E0 "à" */
    0x30, 0x30, 0x03, 0xc0, 0xcf, 0xbf, 0x66, 0xfc,
    0xec,

    /* U+00E1 "á" */
    0x18, 0x60, 0x03, 0xc0, 0xcf, 0xbf, 0x66, 0xfc,
    0xec,

    /* U+00E2 "â" */
    0x30, 0xd0, 0x03, 0xc0, 0xcf, 0xbf, 0x66, 0xfe,
    0xec,

    /* U+00E4 "ä" */
    0x6c, 0x01, 0xe0, 0x67, 0xdf, 0xb3, 0x7e, 0x76,

    /* U+00E5 "å" */
    0x38, 0x88, 0xe0, 0x07, 0x81, 0x9f, 0x7e, 0xcd,
    0xf9, 0xd8,

    /* U+00E6 "æ" */
    0xec, 0x7c, 0x6b, 0xdd, 0xfb, 0x3e, 0xb7,

    /* U+00E7 "ç" */
    0x7d, 0xff, 0x1e, 0x0c, 0x7f, 0xdf, 0x04, 0x18,

    /* U+00E8 "è" */
    0x30, 0x30, 0x03, 0xef, 0xf8, 0xff, 0xe0, 0xfe,
    0xf8,

    /* U+00E9 "é" */
    0x18, 0x60, 0x03, 0xef, 0xf8, 0xff, 0xe0, 0xfe,
    0xf8,

    /* U+00EA "ê" */
    0x30, 0xd0, 0x03, 0xef, 0xf8, 0xff, 0xe0, 0xfe,
    0xf8,

    /* U+00EB "ë" */
    0x6c, 0x01, 0xf7, 0xfc, 0x7f, 0xf0, 0x7f, 0x7c,

    /* U+00EC "ì" */
    0xc6, 0x0e, 0xe6, 0x66, 0xff,

    /* U+00ED "í" */
    0x6c, 0x0e, 0xe6, 0x66, 0xff,

    /* U+00EE "î" */
    0x6d, 0x0e, 0xe6, 0x66, 0xff,

    /* U+00EF "ï" */
    0xcc, 0x07, 0x1c, 0x30, 0xc3, 0x1e, 0x78,

    /* U+00F1 "ñ" */
    0x32, 0x98, 0x06, 0xef, 0xec, 0xd9, 0xb3, 0x66,
    0xcc,

    /* U+00F2 "ò" */
    0x30, 0x30, 0x03, 0xef, 0xf8, 0xf1, 0xe3, 0xfe,
    0xf8,

    /* U+00F3 "ó" */
    0x18, 0x60, 0x03, 0xef, 0xf8, 0xf1, 0xe3, 0xfe,
    0xf8,

    /* U+00F4 "ô" */
    0x38, 0xd8, 0x03, 0xef, 0xf8, 0xf1, 0xe3, 0xfe,
    0xf8,

    /* U+00F6 "ö" */
    0x6c, 0x01, 0xf7, 0xfc, 0x78, 0xf1, 0xff, 0x7c,

    /* U+00F7 "÷" */
    0x31, 0x81, 0xff, 0x80, 0xc6,

    /* U+00F9 "ù" */
    0x60, 0x60, 0x06, 0x6c, 0xd9, 0xb3, 0x66, 0xfe,
    0xec,

    /* U+00FA "ú" */
    0x18, 0x60, 0x06, 0x6c, 0xd9, 0xb3, 0x66, 0xfe,
    0xec,

    /* U+00FB "û" */
    0x30, 0xd0, 0x06, 0x6c, 0xd9, 0xb3, 0x66, 0xfe,
    0xec,

    /* U+00FC "ü" */
    0xcc, 0x03, 0x36, 0x6c, 0xd9, 0xb3, 0x7f, 0x76,

    /* U+00FF "ÿ" */
    0x66, 0x03, 0x9b, 0x36, 0x6f, 0xcf, 0x83, 0x7e,
    0xf8,

    /* U+0192 "ƒ" */
    0x1c, 0xf3, 0x0c, 0xff, 0xf3, 0x0c, 0x30, 0xcf,
    0x38,

    /* U+0393 "Γ" */
    0xff, 0xfd, 0x9b, 0x06, 0x0c, 0x18, 0x30, 0xf9,
    0xf0,

    /* U+0398 "Θ" */
    0x38, 0xdb, 0x1e, 0x3f, 0xff, 0xf1, 0xe3, 0xc6,
    0xd8, 0xe0,

    /* U+03A3 "Σ" */
    0xff, 0xff, 0x83, 0x83, 0x83, 0x0e, 0x38, 0xe1,
    0xff, 0xf8,

    /* U+03A6 "Φ" */
    0x78, 0xc7, 0xbf, 0xb6, 0xdb, 0x7f, 0x78, 0xc7,
    0x80,

    /* U+03A9 "Ω" */
    0x38, 0xfb, 0x1e, 0x3c, 0x78, 0xf1, 0xa2, 0x6d,
    0xdf, 0xb8,

    /* U+03B1 "α" */
    0x77, 0xff, 0x36, 0x6c, 0xd9, 0xff, 0xbb,

    /* U+03B4 "δ" */
    0x38, 0xf9, 0x99, 0x83, 0x8d, 0xb1, 0xe3, 0xc6,
    0xf8, 0xe0,

    /* U+03B5 "ε" */
    0x1e, 0x7d, 0x86, 0x0c, 0x1f, 0xff, 0xe0, 0xc0,
    0xc0, 0xf8, 0xf0,

    /* U+03C0 "π" */
    0x73, 0xff, 0xb3, 0x66, 0xcd, 0xbb, 0xe7,

    /* U+03C3 "σ" */
    0x06, 0xff, 0xe6, 0x6c, 0xd9, 0xbf, 0x3c,

    /* U+03C4 "τ" */
    0x73, 0xfe, 0xf1, 0x83, 0x06, 0xcf, 0x8e,

    /* U+03C6 "φ" */
    0x06, 0x11, 0xf6, 0xbd, 0x7a, 0xf5, 0xbe, 0x10,
    0x40, 0x86, 0x00,

    /* U+2022 "•" */
    0xf0,

    /* U+203C "‼" */
    0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xc0, 0x03, 0x3c,
    0xc0,

    /* U+207F "ⁿ" */
    0x69, 0x99, 0x99,

    /* U+20A7 "₧" */
    0xf9, 0xfb, 0x36, 0x6f, 0xdf, 0x31, 0x67, 0xc5,
    0x8b, 0x18,

    /* U+2190 "←" */
    0x20, 0x83, 0xff, 0xf4, 0x04, 0x00,

    /* U+2191 "↑" */
    0x31, 0xeb, 0x4c, 0x30, 0xc3, 0x0c, 0x30, 0xc0,

    /* U+2192 "→" */
    0x08, 0x0b, 0xff, 0xf0, 0x41, 0x00,

    /* U+2193 "↓" */
    0x30, 0xc3, 0x0c, 0x30, 0xc3, 0x2d, 0x78, 0xc0,

    /* U+2194 "↔" */
    0x45, 0xff, 0xfa, 0x20,

    /* U+2195 "↕" */
    0x31, 0xeb, 0x4c, 0x30, 0xc3, 0x2d, 0x78, 0xc0,

    /* U+21A8 "↨" */
    0x31, 0xe3, 0x0c, 0x30, 0xc3, 0x1e, 0x33, 0xf0,

    /* U+2219 "∙" */
    0xff, 0x80,

    /* U+221A "√" */
    0x3e, 0x7c, 0xc1, 0x83, 0x16, 0x3c, 0x38, 0x30,
    0x60,

    /* U+221E "∞" */
    0x6d, 0xfe, 0x4c, 0x99, 0x3f, 0xdb, 0x00,

    /* U+221F "∟" */
    0xc1, 0x83, 0xff, 0xf0,

    /* U+2229 "∩" */
    0x38, 0xfb, 0x1e, 0x3c, 0x78, 0xf1, 0xe3, 0xc7,
    0x8c,

    /* U+2248 "≈" */
    0x47, 0x6e, 0x24, 0x76, 0xe2,

    /* U+2261 "≡" */
    0xfe, 0x03, 0xf8, 0x0f, 0xe0,

    /* U+2264 "≤" */
    0x0c, 0x30, 0xc3, 0x03, 0x03, 0x03, 0x00, 0xff,
    0xfc,

    /* U+2265 "≥" */
    0x60, 0x60, 0x60, 0x61, 0x86, 0x18, 0x00, 0xff,
    0xfc,

    /* U+2302 "⌂" */
    0x10, 0x20, 0xe1, 0x46, 0xc8, 0xbf, 0x80,

    /* U+2310 "⌐" */
    0xff, 0xff, 0x06, 0x0c, 0x00,

    /* U+2320 "⌠" */
    0x6f, 0xdd, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,

    /* U+2321 "⌡" */
    0x33, 0x33, 0x33, 0x33, 0x3b, 0xbf, 0x60,

    /* U+2500 "─" */
    0xff,

    /* U+2502 "│" */
    0xff, 0xfe,

    /* U+250C "┌" */
    0xfc, 0x21, 0x08, 0x42, 0x10, 0x80,

    /* U+2510 "┐" */
    0xf1, 0x11, 0x11, 0x11, 0x10,

    /* U+2514 "└" */
    0x84, 0x21, 0x08, 0x42, 0x1f,

    /* U+2518 "┘" */
    0x11, 0x11, 0x11, 0x1f,

    /* U+251C "├" */
    0x84, 0x21, 0x08, 0x42, 0x1f, 0x84, 0x21, 0x08,
    0x42, 0x00,

    /* U+2524 "┤" */
    0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x10,

    /* U+252C "┬" */
    0xff, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10,

    /* U+2534 "┴" */
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xff,

    /* U+253C "┼" */
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xff,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,

    /* U+2550 "═" */
    0xff, 0x00, 0xff,

    /* U+2551 "║" */
    0xb6, 0xdb, 0x6d, 0xb6, 0xdb, 0x68,

    /* U+2552 "╒" */
    0xfc, 0x3f, 0x08, 0x42, 0x10, 0x84, 0x00,

    /* U+2553 "╓" */
    0xfe, 0x8a, 0x28, 0xa2, 0x8a, 0x28, 0xa0,

    /* U+2554 "╔" */
    0xfe, 0x0b, 0xe8, 0xa2, 0x8a, 0x28, 0xa2, 0x80,

    /* U+2555 "╕" */
    0xf1, 0xf1, 0x11, 0x11, 0x11,

    /* U+2556 "╖" */
    0xf9, 0x4a, 0x52, 0x94, 0xa5, 0x28,

    /* U+2557 "╗" */
    0xf8, 0x7a, 0x52, 0x94, 0xa5, 0x29, 0x40,

    /* U+2558 "╘" */
    0x84, 0x21, 0x08, 0x43, 0xf0, 0xf8,

    /* U+2559 "╙" */
    0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x3f,

    /* U+255A "╚" */
    0xa2, 0x8a, 0x28, 0xa2, 0xf8, 0x3f,

    /* U+255B "╛" */
    0x11, 0x11, 0x11, 0xf1, 0xf0,

    /* U+255C "╜" */
    0x29, 0x4a, 0x52, 0x94, 0xbf,

    /* U+255D "╝" */
    0x29, 0x4a, 0x52, 0xf4, 0x3f,

    /* U+255E "╞" */
    0x84, 0x21, 0x08, 0x43, 0xff, 0x84, 0x21, 0x08,
    0x42, 0x00,

    /* U+255F "╟" */
    0xa2, 0x8a, 0x28, 0xa2, 0x8a, 0x2f, 0xa2, 0x8a,
    0x28, 0xa2, 0x8a, 0x00,

    /* U+2560 "╠" */
    0xa2, 0x8a, 0x28, 0xa2, 0x8b, 0xef, 0xa2, 0x8a,
    0x28, 0xa2, 0x8a, 0x00,

    /* U+2561 "╡" */
    0x11, 0x11, 0x11, 0xff, 0x11, 0x11, 0x11, 0x10,

    /* U+2562 "╢" */
    0x29, 0x4a, 0x52, 0x94, 0xbd, 0x29, 0x4a, 0x52,
    0x94, 0xa0,

    /* U+2563 "╣" */
    0x29, 0x4a, 0x52, 0x97, 0xbd, 0x29, 0x4a, 0x52,
    0x94, 0xa0,

    /* U+2564 "╤" */
    0xff, 0x00, 0xff, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10,

    /* U+2565 "╥" */
    0xff, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x28,

    /* U+2566 "╦" */
    0xff, 0x00, 0xef, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x28, 0x28,

    /* U+2567 "╧" */
    0x10, 0x10, 0x10, 0x10, 0x10, 0xff, 0x00, 0xff,

    /* U+2568 "╨" */
    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0xff,

    /* U+2569 "╩" */
    0x28, 0x28, 0x28, 0x28, 0x28, 0xef, 0x00, 0xff,

    /* U+256A "╪" */
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xff, 0xff,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,

    /* U+256B "╫" */
    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0xff,
    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,

    /* U+256C "╬" */
    0x28, 0x28, 0x28, 0x28, 0x28, 0xef, 0x00, 0xef,
    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,

    /* U+2580 "▀" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+2584 "▄" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+2588 "█" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+258C "▌" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,

    /* U+2590 "▐" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,

    /* U+2591 "░" */
    0x23, 0x10, 0x8d, 0x58, 0x84, 0x62, 0x11, 0x88,
    0x46, 0x21, 0x18, 0x84, 0x62, 0x00,

    /* U+2592 "▒" */
    0x55, 0xaa, 0x55, 0xff, 0xaa, 0x55, 0xaa, 0x55,
    0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,

    /* U+2593 "▓" */
    0xee, 0xbb, 0xee, 0xff, 0xbb, 0xee, 0xbb, 0xee,
    0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb,

    /* U+25A0 "■" */
    0xff, 0xff, 0xf8,

    /* U+25AC "▬" */
    0xff, 0xff, 0xf8,

    /* U+25B2 "▲" */
    0x23, 0xbe,

    /* U+25BA "►" */
    0x83, 0x0e, 0x3c, 0xfb, 0xef, 0x38, 0xc2, 0x00,

    /* U+25BC "▼" */
    0xfb, 0x88,

    /* U+25C4 "◄" */
    0x04, 0x31, 0xcf, 0x7d, 0xf3, 0xc7, 0x0c, 0x10,

    /* U+25CB "○" */
    0x3c, 0x7e, 0xe7, 0xc3, 0xc3, 0xe7, 0x7e, 0x3c,

    /* U+25D8 "◘" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xe7,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+25D9 "◙" */
    0xff, 0xff, 0xff, 0xff, 0xc3, 0x99, 0x3c, 0x3c,
    0x18, 0x81, 0xc3, 0xff, 0xff, 0xff, 0xff,

    /* U+263A "☺" */
    0x7e, 0x81, 0xa5, 0xa5, 0x81, 0x99, 0xa5, 0x99,
    0x81, 0x7e,

    /* U+263B "☻" */
    0x7e, 0xff, 0xdb, 0xdb, 0xff, 0xe7, 0xdb, 0xe7,
    0xff, 0x7e,

    /* U+263C "☼" */
    0x11, 0x25, 0xf3, 0x6c, 0x6d, 0x9f, 0x49, 0x10,

    /* U+2640 "♀" */
    0x76, 0xe3, 0x1d, 0xb8, 0x9f, 0x21, 0x00,

    /* U+2642 "♂" */
    0x23, 0xaa, 0x47, 0x6e, 0x31, 0xdb, 0x80,

    /* U+2660 "♠" */
    0x10, 0x71, 0xf7, 0xff, 0xff, 0xf5, 0x88, 0x10,
    0x70,

    /* U+2663 "♣" */
    0x10, 0x71, 0xf1, 0xc5, 0x5f, 0xff, 0xaa, 0x10,
    0x70,

    /* U+2665 "♥" */
    0x6d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbe, 0x38,
    0x20,

    /* U+2666 "♦" */
    0x10, 0x20, 0xe1, 0xc7, 0xdf, 0xce, 0x1c, 0x10,
    0x20,

    /* U+266A "♪" */
    0x08, 0x10, 0x30, 0x70, 0xa1, 0x0e, 0x3c, 0xf8,
    0xe0,

    /* U+266B "♫" */
    0x20, 0x60, 0xa1, 0xb2, 0xac, 0xf8, 0xe1, 0x06,
    0x1c, 0x30,

    /* U+3044 "い" */
    0x00,

    /* U+3046 "う" */
    0x00,

    /* U+304B "か" */
    0x00,

    /* U+3057 "し" */
    0x00,

    /* U+306E "の" */
    0x00,

    /* U+3093 "ん" */
    0x00
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 120, .box_w = 2, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 120, .box_w = 6, .box_h = 3, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 7, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 17, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 27, .adv_w = 120, .box_w = 7, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 38, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 48, .adv_w = 120, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 50, .adv_w = 120, .box_w = 5, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 57, .adv_w = 120, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 71, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 76, .adv_w = 120, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 78, .adv_w = 120, .box_w = 7, .box_h = 2, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 80, .adv_w = 120, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 82, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 91, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 101, .adv_w = 120, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 118, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 128, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 148, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 188, .adv_w = 120, .box_w = 2, .box_h = 9, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 191, .adv_w = 120, .box_w = 2, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 194, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 120, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 210, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 220, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 229, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 239, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 268, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 298, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 308, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 120, .box_w = 4, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 334, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 364, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 374, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 384, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 394, .adv_w = 120, .box_w = 7, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 405, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 415, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 434, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 444, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 454, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 464, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 474, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 483, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 493, .adv_w = 120, .box_w = 4, .box_h = 11, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 499, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 508, .adv_w = 120, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 514, .adv_w = 120, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 516, .adv_w = 120, .box_w = 8, .box_h = 2, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 518, .adv_w = 120, .box_w = 3, .box_h = 4, .ofs_x = 3, .ofs_y = 7},
    {.bitmap_index = 520, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 536, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 543, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 553, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 560, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 570, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 578, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 588, .adv_w = 120, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 593, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 602, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 612, .adv_w = 120, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 618, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 625, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 632, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 639, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 647, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 655, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 662, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 669, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 679, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 686, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 693, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 700, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 707, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 715, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 722, .adv_w = 120, .box_w = 4, .box_h = 11, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 728, .adv_w = 120, .box_w = 2, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 731, .adv_w = 120, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 737, .adv_w = 120, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 740, .adv_w = 120, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 747, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 748, .adv_w = 120, .box_w = 2, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 751, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 761, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 771, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 780, .adv_w = 120, .box_w = 5, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 787, .adv_w = 120, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 794, .adv_w = 120, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 800, .adv_w = 120, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 805, .adv_w = 120, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 810, .adv_w = 120, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 821, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 829, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 838, .adv_w = 120, .box_w = 1, .box_h = 2, .ofs_x = 3, .ofs_y = 4},
    {.bitmap_index = 839, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 847, .adv_w = 120, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 853, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 863, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 873, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 883, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 893, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 903, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 913, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 923, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 933, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 943, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 953, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 963, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 973, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 982, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 991, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1000, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1008, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1018, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1025, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1033, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1042, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1051, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1060, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1068, .adv_w = 120, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1073, .adv_w = 120, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1078, .adv_w = 120, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1083, .adv_w = 120, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1090, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1099, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1108, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1117, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1126, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1134, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1139, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1148, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1157, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1166, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1174, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1183, .adv_w = 120, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 1192, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1201, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1211, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1221, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1230, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1240, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1247, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1257, .adv_w = 120, .box_w = 7, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1268, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1275, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1282, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1289, .adv_w = 120, .box_w = 7, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1300, .adv_w = 120, .box_w = 2, .box_h = 2, .ofs_x = 3, .ofs_y = 3},
    {.bitmap_index = 1301, .adv_w = 120, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1310, .adv_w = 120, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 1313, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1323, .adv_w = 120, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1329, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1337, .adv_w = 120, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1343, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1351, .adv_w = 120, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1355, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1363, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1371, .adv_w = 120, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 1373, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1382, .adv_w = 120, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1389, .adv_w = 120, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1393, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1402, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 1407, .adv_w = 120, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1412, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1421, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1430, .adv_w = 120, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1437, .adv_w = 120, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1442, .adv_w = 120, .box_w = 4, .box_h = 14, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1449, .adv_w = 120, .box_w = 4, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1456, .adv_w = 120, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1457, .adv_w = 120, .box_w = 1, .box_h = 15, .ofs_x = 3, .ofs_y = -4},
    {.bitmap_index = 1459, .adv_w = 120, .box_w = 5, .box_h = 9, .ofs_x = 3, .ofs_y = -4},
    {.bitmap_index = 1465, .adv_w = 120, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1470, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 3, .ofs_y = 4},
    {.bitmap_index = 1475, .adv_w = 120, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1479, .adv_w = 120, .box_w = 5, .box_h = 15, .ofs_x = 3, .ofs_y = -4},
    {.bitmap_index = 1489, .adv_w = 120, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1497, .adv_w = 120, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1506, .adv_w = 120, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1514, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1529, .adv_w = 120, .box_w = 8, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1532, .adv_w = 120, .box_w = 3, .box_h = 15, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1538, .adv_w = 120, .box_w = 5, .box_h = 10, .ofs_x = 3, .ofs_y = -4},
    {.bitmap_index = 1545, .adv_w = 120, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1552, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1560, .adv_w = 120, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1565, .adv_w = 120, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1571, .adv_w = 120, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1578, .adv_w = 120, .box_w = 5, .box_h = 9, .ofs_x = 3, .ofs_y = 3},
    {.bitmap_index = 1584, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 1590, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 1596, .adv_w = 120, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1601, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1606, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1611, .adv_w = 120, .box_w = 5, .box_h = 15, .ofs_x = 3, .ofs_y = -4},
    {.bitmap_index = 1621, .adv_w = 120, .box_w = 6, .box_h = 15, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1633, .adv_w = 120, .box_w = 6, .box_h = 15, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1645, .adv_w = 120, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1653, .adv_w = 120, .box_w = 5, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1663, .adv_w = 120, .box_w = 5, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1673, .adv_w = 120, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1683, .adv_w = 120, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1692, .adv_w = 120, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1702, .adv_w = 120, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1710, .adv_w = 120, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1718, .adv_w = 120, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1726, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1741, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1756, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1771, .adv_w = 120, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1778, .adv_w = 120, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1785, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1800, .adv_w = 120, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1808, .adv_w = 120, .box_w = 4, .box_h = 15, .ofs_x = 4, .ofs_y = -4},
    {.bitmap_index = 1816, .adv_w = 120, .box_w = 7, .box_h = 15, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 1830, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1845, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1860, .adv_w = 120, .box_w = 3, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 1863, .adv_w = 120, .box_w = 7, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1866, .adv_w = 120, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1868, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1876, .adv_w = 120, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1878, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1886, .adv_w = 120, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1894, .adv_w = 120, .box_w = 8, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1908, .adv_w = 120, .box_w = 8, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1923, .adv_w = 120, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1933, .adv_w = 120, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1943, .adv_w = 120, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1951, .adv_w = 120, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1958, .adv_w = 120, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1965, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1974, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1983, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1992, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2001, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2010, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 2020, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2021, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2022, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2023, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2024, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2025, .adv_w = 120, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_1[] = {
    0x00, 0x01, 0x02, 0x03, 0x05, 0x07, 0x0a, 0x0b,
    0x0c, 0x10, 0x11, 0x12, 0x15, 0x16, 0x17, 0x1a,
    0x1b, 0x1c, 0x1d, 0x1f, 0x24, 0x25, 0x26, 0x27,
    0x29, 0x31, 0x36, 0x3c, 0x3f, 0x40, 0x41, 0x42,
    0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
    0x4c, 0x4d, 0x4e, 0x4f, 0x51, 0x52, 0x53, 0x54,
    0x56, 0x57, 0x59, 0x5a, 0x5b, 0x5c, 0x5f, 0xf2,
    0x2f3, 0x2f8, 0x303, 0x306, 0x309, 0x311, 0x314, 0x315,
    0x320, 0x323, 0x324, 0x326, 0x1f82, 0x1f9c, 0x1fdf, 0x2007,
    0x20f0, 0x20f1, 0x20f2, 0x20f3, 0x20f4, 0x20f5, 0x2108, 0x2179,
    0x217a, 0x217e, 0x217f, 0x2189, 0x21a8, 0x21c1, 0x21c4, 0x21c5,
    0x2262, 0x2270, 0x2280, 0x2281, 0x2460, 0x2462, 0x246c, 0x2470,
    0x2474, 0x2478, 0x247c, 0x2484, 0x248c, 0x2494, 0x249c
};

static const uint16_t unicode_list_3[] = {
    0x00, 0x04, 0x08, 0x0c, 0x10, 0x11, 0x12, 0x13,
    0x20, 0x2c, 0x32, 0x3a, 0x3c, 0x44, 0x4b, 0x58,
    0x59, 0xba, 0xbb, 0xbc, 0xc0, 0xc2, 0xe0, 0xe3,
    0xe5, 0xe6, 0xea, 0xeb, 0xac4, 0xac6, 0xacb, 0xad7,
    0xaee, 0xb13
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 96, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 160, .range_length = 9373, .glyph_id_start = 97,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 103, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 9552, .range_length = 29, .glyph_id_start = 200,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 9600, .range_length = 2836, .glyph_id_start = 229,
        .unicode_list = unicode_list_3, .glyph_id_ofs_list = NULL, .list_length = 34, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
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
    .cmap_num = 4,
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
const lv_font_t lv_font_nix8810_m15 = {
#else
lv_font_t lv_font_nix8810_m15 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif

#if LV_VERSION_CHECK(9, 3, 0)
    .static_bitmap = 1,    /*Bitmaps are stored as const so they are always static if not compressed */
#endif

    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if NIX8810_M15*/
