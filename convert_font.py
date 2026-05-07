#!/usr/bin/env python3
"""Convert a TTF to an Adafruit GFX font header (.h) using freetype-py.
Usage: convert_font.py <ttf> <size> <font_name> [first_char] [last_char] [--gray]
Char range defaults to 0x20..0x3A (space..colon). Pass hex (e.g. 0x30) or
decimal to override. For digits + colon only, use 0x30 0x3A.

With --gray (or --4bpp), emits 4-bit-per-pixel grayscale glyphs (two pixels
per byte, high nibble first) for use with a custom anti-aliased drawer.
The GFXglyph/GFXfont struct layout is unchanged; only the bitmap data is
interpreted differently. Header defines <name>_BPP = 4 (else 1).
"""
import sys
import freetype

def _parse_int(s):
    return int(s, 0)  # accepts 0x.. / 0o.. / decimal

def main():
    args = sys.argv[1:]
    gray = False
    if "--gray" in args:
        gray = True
        args.remove("--gray")
    if "--4bpp" in args:
        gray = True
        args.remove("--4bpp")

    ttf_path = args[0]
    size = int(args[1])
    font_name = args[2]
    first_char = _parse_int(args[3]) if len(args) > 3 else 0x20
    last_char  = _parse_int(args[4]) if len(args) > 4 else 0x3A

    face = freetype.Face(ttf_path)
    face.set_pixel_sizes(0, size)

    bitmaps = bytearray()
    glyphs = []

    for code in range(first_char, last_char + 1):
        load_flags = freetype.FT_LOAD_RENDER
        if not gray:
            load_flags |= freetype.FT_LOAD_TARGET_MONO
        face.load_char(chr(code), load_flags)
        g = face.glyph
        bm = g.bitmap
        width = bm.width
        rows = bm.rows
        pitch = bm.pitch

        offset = len(bitmaps)

        if gray:
            # Grayscale: bm.buffer has one byte per pixel (0..255).
            # Quantize to 4 bits and pack two pixels per output byte
            # (high nibble = first pixel of the pair).
            pixels = []
            for row in range(rows):
                base = row * pitch
                for col in range(width):
                    v = bm.buffer[base + col]
                    pixels.append((v + 8) >> 4 if v < 0xF8 else 0x0F)
            for i in range(0, len(pixels), 2):
                hi = pixels[i] & 0x0F
                lo = pixels[i + 1] & 0x0F if i + 1 < len(pixels) else 0
                bitmaps.append((hi << 4) | lo)
        else:
            # Extract 1-bit pixels row by row
            bits = []
            for row in range(rows):
                for col in range(width):
                    byte_idx = row * pitch + (col >> 3)
                    bit_idx = 7 - (col & 7)
                    bits.append(1 if (bm.buffer[byte_idx] >> bit_idx) & 1 else 0)
            # Pack bits into bytes MSB first
            for i in range(0, len(bits), 8):
                val = 0
                for b in range(8):
                    if i + b < len(bits):
                        val |= bits[i + b] << (7 - b)
                bitmaps.append(val)

        xAdvance = min(g.advance.x >> 6, 255)
        xOffset = g.bitmap_left
        yOffset = -g.bitmap_top
        # Clamp width/height for GFXglyph uint8_t fields
        w_clamped = min(width, 255)
        h_clamped = min(rows, 255)
        glyphs.append((offset, w_clamped, h_clamped, xAdvance, xOffset, yOffset))

    # Write header
    out_path = font_name + ".h"
    with open(out_path, "w") as f:
        f.write(f"// Generated from {ttf_path} at {size}px\n")
        f.write(f"// Characters 0x{first_char:02X}..0x{last_char:02X}\n")
        f.write(f"// Format: {'4bpp grayscale' if gray else '1bpp monochrome'}\n")
        f.write("#pragma once\n")
        f.write("#include <Adafruit_GFX.h>\n\n")
        f.write(f"#define {font_name}_BPP {4 if gray else 1}\n\n")

        f.write(f"static const uint8_t {font_name}Bitmaps[] PROGMEM = {{\n")
        for i in range(0, len(bitmaps), 12):
            chunk = bitmaps[i:i+12]
            f.write("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n\n")

        f.write(f"static const GFXglyph {font_name}Glyphs[] PROGMEM = {{\n")
        for i, (off, w, h, xAdv, xOff, yOff) in enumerate(glyphs):
            ch = first_char + i
            c = chr(ch) if 0x21 <= ch <= 0x7E else ' '
            f.write(f"  {{ {off:5d}, {w:3d}, {h:3d}, {xAdv:3d}, {xOff:4d}, {yOff:4d} }},  // 0x{ch:02X} '{c}'\n")
        f.write("};\n\n")

        asc = face.size.ascender >> 6
        desc = face.size.descender >> 6
        f.write(f"static const GFXfont {font_name} PROGMEM = {{\n")
        f.write(f"  (uint8_t  *){font_name}Bitmaps,\n")
        f.write(f"  (GFXglyph *){font_name}Glyphs,\n")
        f.write(f"  0x{first_char:02X}, 0x{last_char:02X},\n")
        f.write(f"  {asc - desc}\n")
        f.write("};\n")

    print(f"Wrote {out_path} ({len(bitmaps)} bitmap bytes, {len(glyphs)} glyphs)")

if __name__ == "__main__":
    main()
