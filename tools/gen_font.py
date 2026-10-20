#!/usr/bin/env python3
"""Generate Core/Src/font5x7.c from ASCII-art glyphs.

Each glyph is 5 columns x 7 rows, '#' = lit pixel.  Data is stored
column-major with bit0 = top row, i.e. exactly how the SSD1306 wants
one 8-pixel-high cell of GDDRAM, so drawing a glyph is a direct copy
of 5 bytes into the framebuffer page.

Usage: python3 tools/gen_font.py > Core/Src/font5x7.c
"""

import sys

# Glyphs that matter for this app: digits, A-Z, and the punctuation the
# Casio layout uses ("MON 04.09.2026", "12:34:56", "ALM", "%", ...).
ART = {
    " ": ".....\n.....\n.....\n.....\n.....\n.....\n.....",
    "!": "..#..\n..#..\n..#..\n..#..\n..#..\n.....\n..#..",
    '"': ".###.\n.#.#.\n.#.#.\n.....\n.....\n.....\n.....",
    "#": ".#.#.\n#####\n.#.#.\n.#.#.\n#####\n.#.#.\n.....",
    "%": "##..#\n##.#.\n...#.\n..#..\n.#...\n.#.##\n#..##",
    "'": "..#..\n..#..\n.....\n.....\n.....\n.....\n.....",
    "(": "...#.\n..#..\n.#...\n.#...\n.#...\n..#..\n...#.",
    ")": ".#...\n..#..\n...#.\n...#.\n...#.\n..#..\n.#...",
    "*": ".....\n.#.#.\n..#..\n#####\n..#..\n.#.#.\n.....",
    "+": ".....\n..#..\n..#..\n#####\n..#..\n..#..\n.....",
    ",": ".....\n.....\n.....\n.....\n..##.\n..#..\n.#...",
    "-": ".....\n.....\n.....\n#####\n.....\n.....\n.....",
    ".": ".....\n.....\n.....\n.....\n.....\n..##.\n..##.",
    "/": "....#\n....#\n...#.\n..#..\n.#...\n#....\n#....",
    "0": ".###.\n#..##\n#.#.#\n##..#\n#...#\n#...#\n.###.",
    "1": "..#..\n.##..\n..#..\n..#..\n..#..\n..#..\n.###.",
    "2": ".###.\n#...#\n....#\n...#.\n..#..\n.#...\n#####",
    "3": "#####\n...#.\n..#..\n.##..\n...#.\n#...#\n.###.",
    "4": "...#.\n..##.\n.#.#.\n#..#.\n#####\n...#.\n...#.",
    "5": "#####\n#....\n####.\n....#\n....#\n#...#\n.###.",
    "6": ".###.\n#....\n#....\n####.\n#...#\n#...#\n.###.",
    "7": "#####\n....#\n...#.\n..#..\n.#...\n.#...\n.#...",
    "8": ".###.\n#...#\n#...#\n.###.\n#...#\n#...#\n.###.",
    "9": ".###.\n#...#\n#...#\n.####\n....#\n....#\n.###.",
    ":": ".....\n..##.\n..##.\n.....\n..##.\n..##.\n.....",
    ";": ".....\n..##.\n..##.\n.....\n..##.\n..#..\n.#...",
    "<": "...#.\n..#..\n.#...\n#....\n.#...\n..#..\n...#.",
    "=": ".....\n.....\n#####\n.....\n#####\n.....\n.....",
    ">": ".#...\n..#..\n...#.\n....#\n...#.\n..#..\n.#...",
    "?": ".###.\n#...#\n...#.\n..#..\n..#..\n.....\n..#..",
    "A": ".###.\n#...#\n#...#\n#####\n#...#\n#...#\n#...#",
    "B": "####.\n#...#\n#...#\n####.\n#...#\n#...#\n####.",
    "C": ".###.\n#...#\n#....\n#....\n#....\n#...#\n.###.",
    "D": "####.\n#...#\n#...#\n#...#\n#...#\n#...#\n####.",
    "E": "#####\n#....\n#....\n####.\n#....\n#....\n#####",
    "F": "#####\n#....\n#....\n####.\n#....\n#....\n#....",
    "G": ".###.\n#...#\n#....\n#.###\n#...#\n#...#\n.###.",
    "H": "#...#\n#...#\n#...#\n#####\n#...#\n#...#\n#...#",
    "I": ".###.\n..#..\n..#..\n..#..\n..#..\n..#..\n.###.",
    "J": "..###\n...#.\n...#.\n...#.\n...#.\n#..#.\n.##..",
    "K": "#...#\n#..#.\n#.#..\n##...\n#.#..\n#..#.\n#...#",
    "L": "#....\n#....\n#....\n#....\n#....\n#....\n#####",
    "M": "#...#\n##.##\n#.#.#\n#.#.#\n#...#\n#...#\n#...#",
    "N": "#...#\n##..#\n##..#\n#.#.#\n#..##\n#..##\n#...#",
    "O": ".###.\n#...#\n#...#\n#...#\n#...#\n#...#\n.###.",
    "P": "####.\n#...#\n#...#\n####.\n#....\n#....\n#....",
    "Q": ".###.\n#...#\n#...#\n#...#\n#.#.#\n#..#.\n.##.#",
    "R": "####.\n#...#\n#...#\n####.\n#.#..\n#..#.\n#...#",
    "S": ".####\n#....\n#....\n.###.\n....#\n....#\n####.",
    "T": "#####\n..#..\n..#..\n..#..\n..#..\n..#..\n..#..",
    "U": "#...#\n#...#\n#...#\n#...#\n#...#\n#...#\n.###.",
    "V": "#...#\n#...#\n#...#\n#...#\n#...#\n.#.#.\n..#..",
    "W": "#...#\n#...#\n#...#\n#.#.#\n#.#.#\n##.##\n#...#",
    "X": "#...#\n#...#\n.#.#.\n..#..\n.#.#.\n#...#\n#...#",
    "Y": "#...#\n#...#\n.#.#.\n..#..\n..#..\n..#..\n..#..",
    "Z": "#####\n....#\n...#.\n..#..\n.#...\n#....\n#####",
    "[": ".###.\n.#...\n.#...\n.#...\n.#...\n.#...\n.###.",
    "\\": "#....\n#....\n.#...\n..#..\n...#.\n....#\n....#",
    "]": ".###.\n...#.\n...#.\n...#.\n...#.\n...#.\n.###.",
    "_": ".....\n.....\n.....\n.....\n.....\n.....\n#####",
    "|": "..#..\n..#..\n..#..\n..#..\n..#..\n..#..\n..#..",
}

# "degree" is not ASCII, so it lives in the free slot right after '~' and
# is emitted as code 0x7F (see CH_DEGREE in gfx.h). Used by the sensor
# row ("23.4°C") and by font4x6 as well.
DEGREE = 0x7F
ART_DEGREE = ".##..\n#..#.\n.##..\n.....\n.....\n.....\n....."

FIRST = 0x20
LAST = 0x7F
GLYPH_W = 5
GLYPH_H = 7


def pack(art: str) -> list[int]:
    rows = art.split("\n")
    assert len(rows) == GLYPH_H, f"{len(rows)} rows for:\n{art}"
    cols = [0] * GLYPH_W
    for r, line in enumerate(rows):
        assert len(line) == GLYPH_W, f"row width {line!r} in:\n{art}"
        for c, ch in enumerate(line):
            if ch == "#":
                cols[c] |= 1 << r
            elif ch != ".":
                raise AssertionError(f"bad char {ch!r} in:\n{art}")
    return cols


def main() -> None:
    out = [
        "/* Generated by tools/gen_font.py - do not edit by hand. */",
        "#include <stddef.h>",
        "#include \"font5x7.h\"",
        "",
        "/* Column-major, bit0 = top row (directly blittable to a GDDRAM"
        " page). */",
        "static const uint8_t FONT5X7[FONT5X7_GLYPH_COUNT][FONT5X7_WIDTH] = {",
    ]
    for code in range(FIRST, LAST + 1):
        if code == DEGREE:
            art = ART_DEGREE
            label = "deg"
        else:
            art = ART.get(chr(code), ART[" "])
            label = repr(chr(code)).replace("'", "")
        data = ", ".join(f"0x{b:02X}" for b in pack(art))
        out.append(f"    {{{data}}},  /* {label} */")
    out += [
        "};",
        "",
        "const uint8_t *font5x7_glyph(char ch)",
        "{",
        "    unsigned c = (unsigned char)ch;",
        "",
        "    if (c >= 'a' && c <= 'z') {   /* no lowercase: fold to caps */",
        "        c -= (unsigned char)('a' - 'A');",
        "    }",
        "    if (c < FONT5X7_FIRST || c > FONT5X7_LAST) {",
        "        return NULL;",
        "    }",
        "    return &FONT5X7[c - FONT5X7_FIRST][0];",
        "}",
        "",
    ]
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()