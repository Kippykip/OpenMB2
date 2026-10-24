/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * font.h - the game's bitmap fonts (Text/Text.txt with font.png, and
 * Text/Service.txt with service.png or icons.png).
 *
 * A font is a 256x256 sheet of glyphs. By default it is a grid of 20x32
 * cells, 12 to a row: A-Z, 0-9, then the codes from 0x80 up; lower case
 * and 0xA0-0xBF share the glyphs of A-Z and 0x80-0x9F. The font file then
 * adjusts single glyphs:
 *
 *     chr: charcode: 33 x1: 0 y1: 224 x2: 5 y2: 256   a glyph anywhere
 *     charwidth: A 13          narrower cell (and its lower case)
 *     chardopwidth: A 100      draw size only, the picture stays put
 *
 * Text is laid out in the game's 800x600 screen units, y up, each glyph
 * drawn centred on its cell. Nothing here touches the GPU.
 */
#ifndef OPENMB2_FONT_H
#define OPENMB2_FONT_H

#include "../core/xfile.h"

#include <stdbool.h>

typedef struct FontGlyph {
    float u0, v0, u1, v1;
    float half_width, half_height; /* in screen units at scale 1 */
} FontGlyph;

typedef struct Font {
    FontGlyph glyphs[256];
    int width, height; /* the sheet size the file's pixel numbers refer to */
} Font;

/* How a line sits on its x (0x4271f0). */
enum { FONT_LEFT = 0, FONT_CENTER = 1, FONT_RIGHT = 2 };

/* The default grid, then the font file over it (0x426d30, 0x4279f0). */
bool font_load(Font *font, const char *game_path);

/* Width of a line at scale 1 (0x4260e0). */
float font_text_width(const Font *font, const char *text);

/*
 * Lay out text at (x, y) (its left, centre or right end, by align, and
 * the middle of its first line), as two triangles per character in out.
 * Returns the number of vertices written, never more than max.
 */
int font_layout(const Font *font, float x, float y, float scale, int align, const char *text,
                XVertex *out, int max);

#endif
