/*******************************************************************************
 * Size: 8 px
 * Bpp: 1
 * Opts: --font oldschool_pc_font_pack_v2.2_linux/ttf - Mx (mixed outline+bitmap)/Mx437_Portfolio_6x8.ttf --bpp 1 --size 8 --format lvgl --range 0x20-0xFFFF -o portfolio_6x8
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
    /* U+0000 "\u0000" */
    0x00,

    /* U+0001 "\u0001" */
    0x74, 0x77, 0x1f, 0xc5, 0xc0,

    /* U+0002 "\u0002" */
    0x77, 0xeb, 0xf8, 0xfd, 0xc0,

    /* U+0003 "\u0003" */
    0xdf, 0xff, 0xf7, 0x10,

    /* U+0004 "\u0004" */
    0x23, 0xbe, 0xe2, 0x00,

    /* U+0005 "\u0005" */
    0x73, 0xbf, 0xbd, 0x91, 0xc0,

    /* U+0006 "\u0006" */
    0x23, 0xbf, 0xff, 0x91, 0xc0,

    /* U+0007 "\u0007" */
    0xf0,

    /* U+0008 "\b" */
    0xff, 0xff, 0xf3, 0xcf, 0xff, 0xff,

    /* U+0009 "\t" */
    0x69, 0x96,

    /* U+000A "\n" */
    0xff, 0xfc, 0xed, 0xb7, 0x3f, 0xff,

    /* U+000B "\u000b" */
    0x38, 0xca, 0xe8, 0xc5, 0xc0,

    /* U+000C "\f" */
    0x74, 0x62, 0xe2, 0x38, 0x80,

    /* U+000D "\r" */
    0x39, 0x4e, 0x42, 0x73, 0x00,

    /* U+000E "\u000e" */
    0x7a, 0x5e, 0x95, 0xef, 0x00,

    /* U+000F "\u000f" */
    0x25, 0x5d, 0xb7, 0x54, 0x80,

    /* U+0010 "\u0010" */
    0x86, 0x39, 0xfe, 0x62, 0x00,

    /* U+0011 "\u0011" */
    0x08, 0xcf, 0xf3, 0x8c, 0x20,

    /* U+0012 "\u0012" */
    0x23, 0xbe, 0x4f, 0xb8, 0x80,

    /* U+0013 "\u0013" */
    0xb6, 0xda, 0x28,

    /* U+0014 "\u0014" */
    0x7d, 0x6a, 0xd2, 0x94, 0xa0,

    /* U+0015 "\u0015" */
    0x78, 0x69, 0x61, 0xe0,

    /* U+0016 "\u0016" */
    0xff, 0xfe,

    /* U+0017 "\u0017" */
    0x23, 0xbe, 0x4f, 0xbb, 0xe0,

    /* U+0018 "\u0018" */
    0x23, 0xbe, 0x42, 0x10, 0x80,

    /* U+0019 "\u0019" */
    0x21, 0x08, 0x4f, 0xb8, 0x80,

    /* U+001A "\u001a" */
    0x20, 0xbe, 0x22, 0x00,

    /* U+001B "\u001b" */
    0x22, 0x3e, 0x82, 0x00,

    /* U+001C "\u001c" */
    0x88, 0x8f,

    /* U+001D "\u001d" */
    0x57, 0xd4,

    /* U+001E "\u001e" */
    0x21, 0x1c, 0xef, 0x80,

    /* U+001F "\u001f" */
    0xfb, 0x9c, 0x42, 0x00,

    /* U+0020 " " */
    0x00,

    /* U+0021 "!" */
    0xfa,

    /* U+0022 "\"" */
    0xb6, 0x80,

    /* U+0023 "#" */
    0x52, 0xbe, 0xaf, 0xa9, 0x40,

    /* U+0024 "$" */
    0x23, 0xe0, 0xe0, 0xf8, 0x80,

    /* U+0025 "%" */
    0xc6, 0x44, 0x44, 0x4c, 0x60,

    /* U+0026 "&" */
    0x64, 0xa8, 0x8a, 0xc9, 0xa0,

    /* U+0027 "'" */
    0xd8,

    /* U+0028 "(" */
    0x2a, 0x48, 0x88,

    /* U+0029 ")" */
    0x88, 0x92, 0xa0,

    /* U+002A "*" */
    0x25, 0x5d, 0x52, 0x00,

    /* U+002B "+" */
    0x21, 0x3e, 0x42, 0x00,

    /* U+002C "," */
    0xd8,

    /* U+002D "-" */
    0xf8,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x08, 0x88, 0x88, 0x00,

    /* U+0030 "0" */
    0x74, 0x67, 0x5c, 0xc5, 0xc0,

    /* U+0031 "1" */
    0x59, 0x24, 0xb8,

    /* U+0032 "2" */
    0x74, 0x42, 0x22, 0x23, 0xe0,

    /* U+0033 "3" */
    0xf8, 0x88, 0x20, 0xc5, 0xc0,

    /* U+0034 "4" */
    0x11, 0x95, 0x2f, 0x88, 0x40,

    /* U+0035 "5" */
    0xfc, 0x3c, 0x10, 0xc5, 0xc0,

    /* U+0036 "6" */
    0x32, 0x21, 0xe8, 0xc5, 0xc0,

    /* U+0037 "7" */
    0xf8, 0x44, 0x42, 0x10, 0x80,

    /* U+0038 "8" */
    0x74, 0x62, 0xe8, 0xc5, 0xc0,

    /* U+0039 "9" */
    0x74, 0x62, 0xf0, 0x89, 0x80,

    /* U+003A ":" */
    0xf3, 0xc0,

    /* U+003B ";" */
    0xf3, 0x60,

    /* U+003C "<" */
    0x12, 0x48, 0x42, 0x10,

    /* U+003D "=" */
    0xf8, 0x3e,

    /* U+003E ">" */
    0x84, 0x21, 0x24, 0x80,

    /* U+003F "?" */
    0x74, 0x42, 0x22, 0x00, 0x80,

    /* U+0040 "@" */
    0x74, 0x67, 0x59, 0x41, 0xc0,

    /* U+0041 "A" */
    0x74, 0x63, 0x1f, 0xc6, 0x20,

    /* U+0042 "B" */
    0xf4, 0x63, 0xe8, 0xc7, 0xc0,

    /* U+0043 "C" */
    0x74, 0x61, 0x08, 0x45, 0xc0,

    /* U+0044 "D" */
    0xe4, 0xa3, 0x18, 0xcb, 0x80,

    /* U+0045 "E" */
    0xfc, 0x21, 0xe8, 0x43, 0xe0,

    /* U+0046 "F" */
    0xfc, 0x21, 0xe8, 0x42, 0x00,

    /* U+0047 "G" */
    0x74, 0x61, 0x0b, 0xc5, 0xe0,

    /* U+0048 "H" */
    0x8c, 0x63, 0xf8, 0xc6, 0x20,

    /* U+0049 "I" */
    0xe9, 0x24, 0xb8,

    /* U+004A "J" */
    0x38, 0x84, 0x21, 0x49, 0x80,

    /* U+004B "K" */
    0x8c, 0xa9, 0x8a, 0x4a, 0x20,

    /* U+004C "L" */
    0x84, 0x21, 0x08, 0x43, 0xe0,

    /* U+004D "M" */
    0x8e, 0xeb, 0x58, 0xc6, 0x20,

    /* U+004E "N" */
    0x8c, 0x73, 0x59, 0xc6, 0x20,

    /* U+004F "O" */
    0x74, 0x63, 0x18, 0xc5, 0xc0,

    /* U+0050 "P" */
    0xf4, 0x63, 0xe8, 0x42, 0x00,

    /* U+0051 "Q" */
    0x74, 0x63, 0x1a, 0xc9, 0xa0,

    /* U+0052 "R" */
    0xf4, 0x63, 0xea, 0x4a, 0x20,

    /* U+0053 "S" */
    0x74, 0x60, 0xe0, 0xc5, 0xc0,

    /* U+0054 "T" */
    0xf9, 0x08, 0x42, 0x10, 0x80,

    /* U+0055 "U" */
    0x8c, 0x63, 0x18, 0xc5, 0xc0,

    /* U+0056 "V" */
    0x8c, 0x63, 0x18, 0xa8, 0x80,

    /* U+0057 "W" */
    0x8c, 0x63, 0x5a, 0xee, 0x20,

    /* U+0058 "X" */
    0x8c, 0x54, 0x45, 0x46, 0x20,

    /* U+0059 "Y" */
    0x8c, 0x62, 0xe2, 0x10, 0x80,

    /* U+005A "Z" */
    0xf8, 0x44, 0x44, 0x43, 0xe0,

    /* U+005B "[" */
    0xf2, 0x49, 0x38,

    /* U+005C "\\" */
    0x82, 0x08, 0x20, 0x80,

    /* U+005D "]" */
    0xe4, 0x92, 0x78,

    /* U+005E "^" */
    0x22, 0xa2,

    /* U+005F "_" */
    0xfc,

    /* U+0060 "`" */
    0xe4,

    /* U+0061 "a" */
    0x70, 0x5f, 0x17, 0x80,

    /* U+0062 "b" */
    0x84, 0x2d, 0x98, 0xc7, 0xc0,

    /* U+0063 "c" */
    0x74, 0x21, 0x17, 0x00,

    /* U+0064 "d" */
    0x08, 0x5b, 0x38, 0xc5, 0xe0,

    /* U+0065 "e" */
    0x74, 0x7f, 0x07, 0x80,

    /* U+0066 "f" */
    0x32, 0x51, 0xe4, 0x21, 0x00,

    /* U+0067 "g" */
    0x7c, 0x5e, 0x17, 0x00,

    /* U+0068 "h" */
    0x84, 0x2d, 0x98, 0xc6, 0x20,

    /* U+0069 "i" */
    0x43, 0x24, 0xb8,

    /* U+006A "j" */
    0x10, 0x31, 0x19, 0x60,

    /* U+006B "k" */
    0x84, 0x23, 0x2a, 0x6a, 0x20,

    /* U+006C "l" */
    0xc9, 0x24, 0xb8,

    /* U+006D "m" */
    0xd5, 0x6b, 0x18, 0x80,

    /* U+006E "n" */
    0xb6, 0x63, 0x18, 0x80,

    /* U+006F "o" */
    0x74, 0x63, 0x17, 0x00,

    /* U+0070 "p" */
    0xf4, 0x7d, 0x08, 0x00,

    /* U+0071 "q" */
    0x7c, 0x5e, 0x10, 0x80,

    /* U+0072 "r" */
    0xb6, 0x61, 0x08, 0x00,

    /* U+0073 "s" */
    0x7c, 0x1c, 0x1f, 0x00,

    /* U+0074 "t" */
    0x42, 0x3c, 0x84, 0x24, 0xc0,

    /* U+0075 "u" */
    0x8c, 0x63, 0x36, 0x80,

    /* U+0076 "v" */
    0x8c, 0x62, 0xa2, 0x00,

    /* U+0077 "w" */
    0x8c, 0x6b, 0x55, 0x00,

    /* U+0078 "x" */
    0x8a, 0x88, 0xa8, 0x80,

    /* U+0079 "y" */
    0x8c, 0x5e, 0x1f, 0x00,

    /* U+007A "z" */
    0xf8, 0x88, 0x8f, 0x80,

    /* U+007B "{" */
    0x34, 0x4c, 0x44, 0x30,

    /* U+007C "|" */
    0xee,

    /* U+007D "}" */
    0xc2, 0x23, 0x22, 0xc0,

    /* U+007E "~" */
    0x6d, 0x80,

    /* U+007F "" */
    0x22, 0xa3, 0xf0,

    /* U+00A0 " " */
    0x00,

    /* U+00A1 "¡" */
    0xbe,

    /* U+00A2 "¢" */
    0x27, 0x88, 0x72,

    /* U+00A3 "£" */
    0x22, 0x91, 0xc4, 0x27, 0xc0,

    /* U+00A5 "¥" */
    0x8a, 0xbe, 0x4f, 0x90, 0x80,

    /* U+00A7 "§" */
    0x78, 0x69, 0x61, 0xe0,

    /* U+00AA "ª" */
    0x75, 0x8e,

    /* U+00AB "«" */
    0x2a, 0xa8, 0xa2, 0x80,

    /* U+00AC "¬" */
    0xf8, 0x42,

    /* U+00B0 "°" */
    0x55, 0x00,

    /* U+00B1 "±" */
    0x21, 0x3e, 0x42, 0x03, 0xe0,

    /* U+00B2 "²" */
    0xe5, 0x70,

    /* U+00B5 "µ" */
    0x4a, 0x5a, 0xa4, 0x40,

    /* U+00B6 "¶" */
    0x7d, 0x6a, 0xd2, 0x94, 0xa0,

    /* U+00B7 "·" */
    0x80,

    /* U+00BA "º" */
    0x55, 0x0e,

    /* U+00BB "»" */
    0xa2, 0x8a, 0xaa, 0x00,

    /* U+00BC "¼" */
    0x8c, 0x88, 0xab, 0x3c, 0x40,

    /* U+00BD "½" */
    0x8c, 0x88, 0xb8, 0x88, 0xe0,

    /* U+00BF "¿" */
    0x20, 0x08, 0x88, 0x45, 0xc0,

    /* U+00C4 "Ä" */
    0x88, 0x1d, 0x18, 0xfe, 0x20,

    /* U+00C5 "Å" */
    0x20, 0x1d, 0x18, 0xfe, 0x20,

    /* U+00C6 "Æ" */
    0x3a, 0xa5, 0xf9, 0x4a, 0x60,

    /* U+00C7 "Ç" */
    0x74, 0x61, 0x17, 0x09, 0xc0,

    /* U+00C9 "É" */
    0x22, 0x3f, 0x0e, 0x43, 0xe0,

    /* U+00D1 "Ñ" */
    0xf8, 0x23, 0x9a, 0xce, 0x20,

    /* U+00D6 "Ö" */
    0x88, 0x1d, 0x18, 0xc5, 0xc0,

    /* U+00DC "Ü" */
    0x88, 0x23, 0x18, 0xc5, 0xc0,

    /* U+00DF "ß" */
    0x74, 0x7d, 0x1c, 0xda, 0x00,

    /* U+00E0 "à" */
    0x41, 0x1c, 0x17, 0xc5, 0xe0,

    /* U+00E1 "á" */
    0x11, 0x1c, 0x17, 0xc5, 0xe0,

    /* U+00E2 "â" */
    0x22, 0x9c, 0x17, 0xc5, 0xe0,

    /* U+00E4 "ä" */
    0x50, 0x1c, 0x17, 0xc5, 0xe0,

    /* U+00E5 "å" */
    0x20, 0x1c, 0x17, 0xc5, 0xe0,

    /* U+00E6 "æ" */
    0xd1, 0x5d, 0x45, 0x80,

    /* U+00E7 "ç" */
    0x74, 0x22, 0xee, 0x00,

    /* U+00E8 "è" */
    0x41, 0x1d, 0x1f, 0xc1, 0xe0,

    /* U+00E9 "é" */
    0x11, 0x1d, 0x1f, 0xc1, 0xe0,

    /* U+00EA "ê" */
    0x22, 0x9d, 0x1f, 0xc1, 0xe0,

    /* U+00EB "ë" */
    0x50, 0x1d, 0x1f, 0xc1, 0xe0,

    /* U+00EC "ì" */
    0x88, 0x64, 0xb8,

    /* U+00ED "í" */
    0x28, 0x64, 0xb8,

    /* U+00EE "î" */
    0x54, 0x64, 0xb8,

    /* U+00EF "ï" */
    0xa0, 0x64, 0xb8,

    /* U+00F1 "ñ" */
    0xf8, 0x2d, 0x98, 0xc4,

    /* U+00F2 "ò" */
    0x41, 0x00, 0xe8, 0xc5, 0xc0,

    /* U+00F3 "ó" */
    0x11, 0x00, 0xe8, 0xc5, 0xc0,

    /* U+00F4 "ô" */
    0x22, 0x80, 0xe8, 0xc5, 0xc0,

    /* U+00F6 "ö" */
    0x50, 0x00, 0xe8, 0xc5, 0xc0,

    /* U+00F7 "÷" */
    0x20, 0x3e, 0x02, 0x00,

    /* U+00F9 "ù" */
    0x41, 0x01, 0x18, 0xcd, 0xa0,

    /* U+00FA "ú" */
    0x11, 0x01, 0x18, 0xcd, 0xa0,

    /* U+00FB "û" */
    0x22, 0x81, 0x18, 0xcd, 0xa0,

    /* U+00FC "ü" */
    0x50, 0x01, 0x18, 0xcd, 0xa0,

    /* U+00FF "ÿ" */
    0x50, 0x23, 0x17, 0x87, 0xc0,

    /* U+0192 "ƒ" */
    0x11, 0x48, 0xe2, 0x51, 0x00,

    /* U+0393 "Γ" */
    0xfc, 0x63, 0x08, 0x42, 0x00,

    /* U+0398 "Θ" */
    0x74, 0x63, 0xf8, 0xc5, 0xc0,

    /* U+03A3 "Σ" */
    0xfa, 0x48, 0x22, 0x27, 0xe0,

    /* U+03A6 "Φ" */
    0xf9, 0x1d, 0x17, 0x13, 0xe0,

    /* U+03A9 "Ω" */
    0x74, 0x63, 0x18, 0xab, 0x60,

    /* U+03B1 "α" */
    0x6c, 0xa5, 0x29, 0x34,

    /* U+03B4 "δ" */
    0x78, 0x46, 0x99, 0x60,

    /* U+03B5 "ε" */
    0x34, 0x8f, 0x84, 0x30,

    /* U+03C0 "π" */
    0xfa, 0x94, 0xa5, 0x28,

    /* U+03C3 "σ" */
    0x7d, 0x29, 0x4a, 0x20,

    /* U+03C4 "τ" */
    0x6d, 0x88, 0x42, 0x10,

    /* U+03C6 "φ" */
    0x0b, 0xa7, 0x5c, 0xba, 0x00,

    /* U+2022 "•" */
    0xf0,

    /* U+203C "‼" */
    0xb6, 0xda, 0x28,

    /* U+207F "ⁿ" */
    0xd6, 0x80,

    /* U+20A7 "₧" */
    0xe4, 0xb9, 0x29, 0xca, 0x40,

    /* U+2190 "←" */
    0x22, 0x3e, 0x82, 0x00,

    /* U+2191 "↑" */
    0x23, 0xbe, 0x42, 0x10, 0x80,

    /* U+2192 "→" */
    0x20, 0xbe, 0x22, 0x00,

    /* U+2193 "↓" */
    0x21, 0x08, 0x4f, 0xb8, 0x80,

    /* U+2194 "↔" */
    0x57, 0xd4,

    /* U+2195 "↕" */
    0x23, 0xbe, 0x4f, 0xb8, 0x80,

    /* U+21A8 "↨" */
    0x23, 0xbe, 0x4f, 0xbb, 0xe0,

    /* U+2219 "∙" */
    0xf0,

    /* U+221A "√" */
    0x39, 0x08, 0x42, 0x51, 0x80,

    /* U+221E "∞" */
    0x55, 0x6a, 0xa0,

    /* U+221F "∟" */
    0x88, 0x8f,

    /* U+2229 "∩" */
    0x74, 0x63, 0x18, 0xc4,

    /* U+2248 "≈" */
    0x6d, 0x80, 0xdb, 0x00,

    /* U+2261 "≡" */
    0xf8, 0x01, 0xf0, 0x03, 0xe0,

    /* U+2264 "≤" */
    0x11, 0x10, 0x41, 0x03, 0xe0,

    /* U+2265 "≥" */
    0x41, 0x04, 0x44, 0x03, 0xe0,

    /* U+2302 "⌂" */
    0x22, 0xa3, 0xf0,

    /* U+2310 "⌐" */
    0xfc, 0x20,

    /* U+2320 "⌠" */
    0x56, 0x49, 0x24,

    /* U+2321 "⌡" */
    0x24, 0x93, 0x50,

    /* U+2500 "─" */
    0xfc,

    /* U+2502 "│" */
    0xff,

    /* U+250C "┌" */
    0xf8, 0x88, 0x80,

    /* U+2510 "┐" */
    0xe4, 0x92,

    /* U+2514 "└" */
    0x88, 0x8f,

    /* U+2518 "┘" */
    0x24, 0xf0,

    /* U+251C "├" */
    0x88, 0x8f, 0x88, 0x88,

    /* U+2524 "┤" */
    0x24, 0xf2, 0x49,

    /* U+252C "┬" */
    0xfc, 0x82, 0x08, 0x20,

    /* U+2534 "┴" */
    0x20, 0x82, 0x3f,

    /* U+253C "┼" */
    0x20, 0x82, 0x3f, 0x20, 0x82, 0x08,

    /* U+2550 "═" */
    0xfc, 0x0f, 0xc0,

    /* U+2551 "║" */
    0xb6, 0xdb, 0x6d,

    /* U+2552 "╒" */
    0xf8, 0xf8, 0x88,

    /* U+2553 "╓" */
    0xfd, 0x29, 0x4a, 0x00,

    /* U+2554 "╔" */
    0xfc, 0x2f, 0x4a, 0x50,

    /* U+2555 "╕" */
    0xe7, 0x92, 0x40,

    /* U+2556 "╖" */
    0xf5, 0x55, 0x50,

    /* U+2557 "╗" */
    0xf1, 0xd5, 0x55,

    /* U+2558 "╘" */
    0x88, 0xf8, 0xf0,

    /* U+2559 "╙" */
    0xa5, 0x29, 0xf0,

    /* U+255A "╚" */
    0xa5, 0x2f, 0x0f, 0x80,

    /* U+255B "╛" */
    0x27, 0x9e,

    /* U+255C "╜" */
    0x55, 0x5f,

    /* U+255D "╝" */
    0x55, 0xd1, 0xf0,

    /* U+255E "╞" */
    0x88, 0xf8, 0xf8, 0x88,

    /* U+255F "╟" */
    0xa5, 0x29, 0x7a, 0x52, 0x94,

    /* U+2560 "╠" */
    0xa5, 0x2f, 0x0b, 0xd2, 0x94,

    /* U+2561 "╡" */
    0x27, 0x9e, 0x49,

    /* U+2562 "╢" */
    0x55, 0x5d, 0x55, 0x55,

    /* U+2563 "╣" */
    0x55, 0xd1, 0xd5, 0x55,

    /* U+2564 "╤" */
    0xfc, 0x0f, 0xc8, 0x20, 0x80,

    /* U+2565 "╥" */
    0xfd, 0x45, 0x14, 0x50,

    /* U+2566 "╦" */
    0xfc, 0x0d, 0xd4, 0x51, 0x40,

    /* U+2567 "╧" */
    0x20, 0x8f, 0xc0, 0xfc,

    /* U+2568 "╨" */
    0x51, 0x45, 0x3f,

    /* U+2569 "╩" */
    0x51, 0x4d, 0xc0, 0xfc,

    /* U+256A "╪" */
    0x20, 0x8f, 0xc8, 0xfc, 0x82, 0x08,

    /* U+256B "╫" */
    0x51, 0x45, 0x3f, 0x51, 0x45, 0x14,

    /* U+256C "╬" */
    0x51, 0x4d, 0xc0, 0xdd, 0x45, 0x14,

    /* U+2580 "▀" */
    0xff, 0xff, 0xff,

    /* U+2584 "▄" */
    0xff, 0xff, 0xff,

    /* U+2588 "█" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+258C "▌" */
    0xff, 0xff, 0xff,

    /* U+2590 "▐" */
    0xff, 0xff, 0xff,

    /* U+2591 "░" */
    0x56, 0xa5, 0x6a, 0x56, 0xa5, 0x6a,

    /* U+2592 "▒" */
    0xd9, 0xbd, 0x9b, 0xd9, 0xbd, 0x9b,

    /* U+2593 "▓" */
    0xfa, 0xfe, 0xfe, 0xbf, 0xbf, 0xaf,

    /* U+25A0 "■" */
    0xff, 0xf0,

    /* U+25AC "▬" */
    0xff, 0xfe,

    /* U+25B2 "▲" */
    0x21, 0x1c, 0xef, 0x80,

    /* U+25BA "►" */
    0x86, 0x39, 0xfe, 0x62, 0x00,

    /* U+25BC "▼" */
    0xfb, 0x9c, 0x42, 0x00,

    /* U+25C4 "◄" */
    0x08, 0xcf, 0xf3, 0x8c, 0x20,

    /* U+25CB "○" */
    0x69, 0x96,

    /* U+25D8 "◘" */
    0xff, 0xff, 0xf3, 0xcf, 0xff, 0xff,

    /* U+25D9 "◙" */
    0xff, 0xfc, 0xed, 0xb7, 0x3f, 0xff,

    /* U+263A "☺" */
    0x74, 0x77, 0x1f, 0xc5, 0xc0,

    /* U+263B "☻" */
    0x77, 0xeb, 0xf8, 0xfd, 0xc0,

    /* U+263C "☼" */
    0x25, 0x5d, 0xb7, 0x54, 0x80,

    /* U+2640 "♀" */
    0x74, 0x62, 0xe2, 0x38, 0x80,

    /* U+2642 "♂" */
    0x38, 0xca, 0xe8, 0xc5, 0xc0,

    /* U+2660 "♠" */
    0x23, 0xbf, 0xff, 0x91, 0xc0,

    /* U+2663 "♣" */
    0x73, 0xbf, 0xbd, 0x91, 0xc0,

    /* U+2665 "♥" */
    0xdf, 0xff, 0xf7, 0x10,

    /* U+2666 "♦" */
    0x23, 0xbe, 0xe2, 0x00,

    /* U+266A "♪" */
    0x39, 0x4e, 0x42, 0x73, 0x00,

    /* U+266B "♫" */
    0x7a, 0x5e, 0x95, 0xef, 0x00,

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
    {.bitmap_index = 0, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 6, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 11, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 15, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 19, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 24, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 29, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 30, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 36, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 38, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 44, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 49, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 59, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 69, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 74, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 79, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 92, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 98, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 103, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 113, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 117, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 121, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 123, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 125, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 129, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 133, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 134, .adv_w = 96, .box_w = 1, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 135, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 137, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 147, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 152, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 157, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 158, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 161, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 164, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 168, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 172, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 173, .adv_w = 96, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 174, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 175, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 179, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 184, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 187, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 192, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 197, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 207, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 212, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 217, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 227, .adv_w = 96, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 229, .adv_w = 96, .box_w = 2, .box_h = 6, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 231, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 235, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 237, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 241, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 246, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 251, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 256, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 271, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 276, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 281, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 286, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 294, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 299, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 304, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 309, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 314, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 334, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 339, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 349, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 359, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 364, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 369, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 374, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 379, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 382, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 386, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 389, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 391, .adv_w = 96, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 392, .adv_w = 96, .box_w = 2, .box_h = 3, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 393, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 402, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 406, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 411, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 415, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 420, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 424, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 429, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 432, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 436, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 441, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 444, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 448, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 452, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 456, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 460, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 464, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 468, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 472, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 477, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 481, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 485, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 489, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 493, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 497, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 501, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 505, .adv_w = 96, .box_w = 1, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 506, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 96, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 512, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 515, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 516, .adv_w = 96, .box_w = 1, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 517, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 520, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 525, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 530, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 534, .adv_w = 96, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 536, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 540, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 542, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 544, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 549, .adv_w = 96, .box_w = 3, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 551, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 555, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 560, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 561, .adv_w = 96, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 563, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 567, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 572, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 577, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 582, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 587, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 592, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 597, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 602, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 607, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 612, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 617, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 622, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 627, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 632, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 637, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 642, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 647, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 652, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 656, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 660, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 665, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 670, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 675, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 680, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 683, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 686, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 689, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 692, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 696, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 701, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 706, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 711, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 716, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 720, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 725, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 730, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 735, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 740, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 745, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 750, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 755, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 760, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 765, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 770, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 775, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 779, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 783, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 787, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 791, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 795, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 799, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 804, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 805, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 808, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 810, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 815, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 819, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 824, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 828, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 833, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 835, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 840, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 845, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 846, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 851, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 854, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 856, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 860, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 864, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 869, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 874, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 879, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 882, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 884, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 887, .adv_w = 96, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 890, .adv_w = 96, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 891, .adv_w = 96, .box_w = 1, .box_h = 8, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 892, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 895, .adv_w = 96, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 897, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 899, .adv_w = 96, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 901, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 905, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 908, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 912, .adv_w = 96, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 915, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 921, .adv_w = 96, .box_w = 6, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 924, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 927, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 930, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 934, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 938, .adv_w = 96, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 941, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 944, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 947, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 950, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 953, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 957, .adv_w = 96, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 959, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 961, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 964, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 968, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 973, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 978, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 981, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 985, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 989, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 994, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 998, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1003, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1007, .adv_w = 96, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1010, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1014, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1020, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1026, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1032, .adv_w = 96, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1035, .adv_w = 96, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1038, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1044, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1047, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 1050, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1056, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1062, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1068, .adv_w = 96, .box_w = 3, .box_h = 4, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 1070, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1072, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1076, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1081, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1085, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1090, .adv_w = 96, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 1092, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1098, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1104, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1109, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1114, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1119, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1124, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1129, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1134, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1139, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1143, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 1147, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1152, .adv_w = 96, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1157, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1158, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1159, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1160, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1161, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1162, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0}
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
        .range_start = 0, .range_length = 128, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 160, .range_length = 9373, .glyph_id_start = 129,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 103, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 9552, .range_length = 29, .glyph_id_start = 232,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 9600, .range_length = 2836, .glyph_id_start = 261,
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
const lv_font_t lv_font_portfolio_6x8 = {
#else
const lv_font_t lv_font_portfolio_6x8 = {
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

#if LV_VERSION_CHECK(9, 3, 0)
    .static_bitmap = 1,    /*Bitmaps are stored as const so they are always static if not compressed */
#endif

    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};

#endif /*#if PORTFOLIO_6X8*/
