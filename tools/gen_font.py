#!/usr/bin/env python3
"""Erzeugt src/oled_font.h: proportionale 5x7-Schrift fuer das OLED-Display.

Grundlage ist LCD_FONT aus luma.core (MIT-Lizenz, (c) Richard Hull und Mitwirkende,
https://github.com/rm-hull/luma.core). Ergaenzt um deutsche Umlaute, ss und Pfeile.

Aufruf:  pip install luma.core  &&  python3 tools/gen_font.py
"""
import os

from luma.core.legacy.font import LCD_FONT

FIRST, LAST = 32, 126


def from_rows(rows):
    """Glyph aus Zeilen ('#' = Pixel) in Spalten-Bytes (Bit 0 = oberste Zeile) umwandeln."""
    width = max(len(r) for r in rows)
    cols = []
    for x in range(width):
        value = 0
        for y, row in enumerate(rows):
            if x < len(row) and row[x] == '#':
                value |= 1 << y
        cols.append(value)
    return cols


def with_dots(base):
    """Kleinbuchstabe mit Umlaut-Punkten in Zeile 0 (Spalten 1 und 3)."""
    cols = list(base)
    cols[1] |= 1
    cols[3] |= 1
    return cols


EXTRA = [  # (Zeichen, Spalten)
    ('ä', with_dots(LCD_FONT[ord('a')][:5])),
    ('ö', with_dots(LCD_FONT[ord('o')][:5])),
    ('ü', with_dots(LCD_FONT[ord('u')][:5])),
    ('Ä', from_rows(['.#.#.', '.###.', '#...#', '#...#', '#####', '#...#', '#...#'])),
    ('Ö', from_rows(['.#.#.', '.###.', '#...#', '#...#', '#...#', '#...#', '.###.'])),
    ('Ü', from_rows(['.#.#.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'])),
    ('ß', from_rows(['.##.', '#..#', '#.#.', '#..#', '#..#', '#.#.', '#...'])),
    ('↓', from_rows(['..#..', '..#..', '..#..', '..#..', '#.#.#', '.###.', '..#..'])),
    ('↑', from_rows(['..#..', '.###.', '#.#.#', '..#..', '..#..', '..#..', '..#..'])),
    ('·', from_rows(['.', '.', '.', '#'])),
]


def trimmed(cols, code):
    cols = list(cols[:5]) + [0] * (5 - len(cols[:5]))
    if code == 32:
        return [0, 0, 0, 0, 0], 3
    width = 5
    while width > 1 and cols[width - 1] == 0:
        width -= 1
    return cols, width


def main():
    glyphs = []
    for code in range(FIRST, LAST + 1):
        cols, width = trimmed(LCD_FONT[code], code)
        # fuehrende Leerspalte entfernen (proportionale Schrift)
        while code != 32 and cols[0] == 0 and width > 1:
            cols = cols[1:] + [0]
            width -= 1
        glyphs.append((chr(code), cols, width))
    for char, cols in EXTRA:
        c, w = trimmed(cols, 0)
        glyphs.append((char, c, w))

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'oled_font.h')
    with open(out, 'w', encoding='utf-8') as handle:
        handle.write('// Automatisch erzeugt von tools/gen_font.py. Nicht von Hand aendern.\n')
        handle.write('// Proportionale 5x7-Schrift, Grundlage: LCD_FONT aus luma.core (MIT-Lizenz, (c) Richard Hull u. a.).\n')
        handle.write('// Je Zeichen 5 Spalten-Bytes (Bit 0 = oben) und die Breite in Pixeln.\n')
        handle.write('#pragma once\n#include <stdint.h>\n\n')
        handle.write('constexpr uint8_t OLED_FONT_FIRST = %d;\n' % FIRST)
        handle.write('constexpr uint8_t OLED_FONT_COUNT = %d;\n' % len(glyphs))
        handle.write('// Sonderzeichen ab Index %d: %s\n' % (LAST - FIRST + 1, ' '.join(c for c, _ in EXTRA)))
        handle.write('static const uint8_t OLED_FONT[][6] = {\n')
        for char, cols, width in glyphs:
            label = char if char not in '\\' else 'backslash'
            handle.write('  {%s, %d},  // %s\n' % (', '.join('0x%02X' % c for c in cols), width, label))
        handle.write('};\n')
    print('%s: %d Zeichen' % (os.path.normpath(out), len(glyphs)))


if __name__ == '__main__':
    main()
