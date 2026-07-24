#!/usr/bin/env python3
"""Convert PNG/JPEG artwork into an LVGL 9 C image descriptor."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SIZE_RE = re.compile(r"^(\d+)x(\d+)$")


def parse_size(value: str) -> tuple[int, int]:
    match = SIZE_RE.match(value)
    if not match:
        raise argparse.ArgumentTypeError("size must be WIDTHxHEIGHT, for example 320x170")

    width = int(match.group(1))
    height = int(match.group(2))
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size dimensions must be positive")

    return width, height


def parse_color(value: str) -> tuple[int, int, int, int]:
    try:
        from PIL import ImageColor
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required. Install it with: python3 -m pip install -r tools/requirements-image.txt"
        ) from exc

    try:
        return ImageColor.getcolor(value, "RGBA")
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def default_symbol(path: Path) -> str:
    symbol = re.sub(r"[^A-Za-z0-9_]", "_", path.stem)
    if not symbol or symbol[0].isdigit():
        symbol = f"img_{symbol}"
    return symbol


def prepare_image(
    input_path: Path,
    size: tuple[int, int] | None,
    fit: str,
    background: tuple[int, int, int, int],
    with_alpha: bool,
) -> Image.Image:
    try:
        from PIL import Image, ImageOps
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required. Install it with: python3 -m pip install -r tools/requirements-image.txt"
        ) from exc

    image = Image.open(input_path)
    image = ImageOps.exif_transpose(image)

    if size:
        if fit == "stretch":
            resample = Image.Resampling.LANCZOS
            image = image.resize(size, resample)
        elif fit == "cover":
            image = ImageOps.fit(image, size, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))
        elif fit == "contain":
            image = image.copy()
            image.thumbnail(size, Image.Resampling.LANCZOS)
            canvas = Image.new("RGBA", size, background)
            x = (size[0] - image.width) // 2
            y = (size[1] - image.height) // 2
            canvas.alpha_composite(image.convert("RGBA"), (x, y))
            image = canvas
        elif fit == "none" and image.size != size:
            raise SystemExit(f"image is {image.width}x{image.height}, expected {size[0]}x{size[1]}")

    return image.convert("RGBA" if with_alpha else "RGB")


def rgb565_bytes(r: int, g: int, b: int, byte_order: str) -> tuple[int, int]:
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    high = (rgb565 >> 8) & 0xFF
    low = rgb565 & 0xFF
    if byte_order == "big":
        return high, low
    return low, high


def format_bytes(values: list[int]) -> str:
    lines = []
    for offset in range(0, len(values), 16):
        chunk = values[offset : offset + 16]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def write_c_file(
    output_path: Path,
    symbol: str,
    image: Image.Image,
    byte_order: str,
    with_alpha: bool,
) -> None:
    values: list[int] = []
    pixels = image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()
    for pixel in pixels:
        if with_alpha:
            r, g, b, a = pixel
        else:
            r, g, b = pixel
            a = 0xFF

        values.extend(rgb565_bytes(r, g, b, byte_order))
        if with_alpha:
            values.append(a)

    color_format = "LV_COLOR_FORMAT_RGB565A8" if with_alpha else "LV_COLOR_FORMAT_RGB565"

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        "\n".join(
            [
                '#include "lvgl.h"',
                "",
                "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
                "#define LV_ATTRIBUTE_MEM_ALIGN",
                "#endif",
                "",
                f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {symbol}_map[] = {{",
                format_bytes(values),
                "};",
                "",
                f"const lv_image_dsc_t {symbol} = {{",
                f"    .header.cf = {color_format},",
                "    .header.magic = LV_IMAGE_HEADER_MAGIC,",
                f"    .header.w = {image.width},",
                f"    .header.h = {image.height},",
                f"    .data_size = sizeof({symbol}_map),",
                f"    .data = {symbol}_map,",
                "};",
                "",
            ]
        )
    )


def cmake_hint(output: Path) -> str:
    try:
        cmake_path = output.relative_to(Path("main"))
    except ValueError:
        return f"Add the generated file to main/CMakeLists.txt if you want it compiled: {output}"

    return f'Add "{cmake_path}" to main/CMakeLists.txt if you want it compiled.'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Source image, for example main/images/gammahex_bg.png")
    parser.add_argument("--output", type=Path, help="Generated C file path")
    parser.add_argument("--symbol", help="C symbol name for the lv_image_dsc_t")
    parser.add_argument("--size", type=parse_size, default=parse_size("320x170"), help="Output size, default 320x170")
    parser.add_argument(
        "--fit",
        choices=("cover", "contain", "stretch", "none"),
        default="cover",
        help="Resize behavior when --size is set, default cover",
    )
    parser.add_argument("--background", default="#000000", help="Contain fill color")
    parser.add_argument("--alpha", action="store_true", help="Emit RGB565A8 instead of RGB565")
    parser.add_argument(
        "--byte-order",
        choices=("little", "big"),
        default="little",
        help="RGB565 byte order, default little for ESP32/LVGL native-endian data",
    )
    args = parser.parse_args()

    if not args.input.is_file():
        raise SystemExit(f"input image not found: {args.input}")

    symbol = args.symbol or default_symbol(args.input)
    if not SYMBOL_RE.match(symbol):
        raise SystemExit(f"invalid C symbol name: {symbol}")

    output = args.output or Path("main/images/generated") / f"{symbol}.c"
    background = parse_color(args.background)
    image = prepare_image(args.input, args.size, args.fit, background, args.alpha)
    write_c_file(output, symbol, image, args.byte_order, args.alpha)

    bytes_per_pixel = 3 if args.alpha else 2
    print(f"Wrote {output} ({image.width}x{image.height}, {image.width * image.height * bytes_per_pixel} bytes)")
    print(cmake_hint(output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
