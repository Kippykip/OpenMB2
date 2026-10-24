/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "font.h"
#include "../core/config.h"
#include "../core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The default grid (0x426d30): 6 rows of 12 cells, 20x32 pixels each. */
static void font_defaults(Font *font)
{
    int row, col, c = 'A', i;

    memset(font, 0, sizeof(*font));
    font->width = font->height = 256;
    for (row = 0; row < 6; row++) {
        for (col = 0; col < 12; col++) {
            FontGlyph *g = &font->glyphs[c];
            g->u0 = (float)col * 20.0f / 256.0f;
            g->u1 = (float)(col + 1) * 20.0f / 256.0f;
            g->v0 = (float)row * 0.125f + 1.0f / 256.0f;
            g->v1 = (float)(row + 1) * 0.125f;
            g->half_width = 10.0f;
            g->half_height = 16.0f;
            if ((c >= 'A' && c <= 'Z') || (c >= 0x80 && c < 0xa0))
                font->glyphs[c + 0x20] = *g;
            /* A-Z, then 0-9, then on from 0x80. */
            c++;
            if (c == '[')
                c = '0';
            else if (c == ':')
                c = 0x80;
        }
    }
    for (i = 0x80; i < 0xa0; i++) {
        font->glyphs[i + 0x40] = font->glyphs[i];
        font->glyphs[i + 0x60] = font->glyphs[i];
    }
    font->glyphs[' '].half_width = 10.0f;
}

/* "X 13": the glyph's byte and a number. */
static bool glyph_value(const char *value, unsigned char *c, float *n)
{
    char name[8];

    if (value == NULL || sscanf(value, "%7s %f", name, n) != 2)
        return false;
    *c = (unsigned char)name[0];
    return true;
}

bool font_load(Font *font, const char *game_path)
{
    Config cfg;
    int i;

    font_defaults(font);
    if (!config_load(&cfg, game_path)) {
        log_warn("font: can't read %s", game_path);
        return false;
    }
    /* In file order: a later line overrides an earlier one (0x4279f0). */
    for (i = 0; i < cfg.count; i++) {
        const char *key = cfg.entries[i].key, *value = cfg.entries[i].value;
        unsigned char c;
        float n;
        FontGlyph *g;

        if (strcmp(key, "width") == 0 && value != NULL) {
            font->width = atoi(value);
        } else if (strcmp(key, "height") == 0 && value != NULL) {
            font->height = atoi(value);
        } else if (strcmp(key, "charwidth") == 0 && glyph_value(value, &c, &n)) {
            g = &font->glyphs[c];
            g->half_width = (n + 1.0f) * 0.5f;
            g->u1 = g->u0 + (n + 1.0f) / 256.0f;
            if (c >= 'A' && c <= 'Z')
                font->glyphs[c + 0x20] = *g;
            if (c >= 0xa0 && c <= 0xc0)
                font->glyphs[c - 0x20] = *g;
        } else if (strcmp(key, "charheight") == 0 && glyph_value(value, &c, &n) && c < 0x80) {
            /* The original reads these three with a signed byte, so codes
               from 0x80 up land outside the table; they are ignored here. */
            g = &font->glyphs[c];
            g->half_height = (n + 1.0f) * 0.5f;
            g->v1 = g->v0 + (n + 1.0f) / 256.0f;
            if (c >= 'A' && c <= 'Z')
                font->glyphs[c + 0x20] = *g;
        } else if (strcmp(key, "chardopwidth") == 0 && glyph_value(value, &c, &n) && c < 0x80) {
            font->glyphs[c].half_width = n * 0.5f;
        } else if (strcmp(key, "chardopheight") == 0 && glyph_value(value, &c, &n) && c < 0x80) {
            font->glyphs[c].half_height = n * 0.5f;
        } else if (strcmp(key, "chr") == 0 && value != NULL) {
            int code;
            float x1, y1, x2, y2;
            if (sscanf(value, "charcode: %d x1: %f y1: %f x2: %f y2: %f", &code, &x1, &y1, &x2, &y2) == 5 &&
                code >= 0 && code < 256) {
                g = &font->glyphs[code];
                g->u0 = x1 / (float)font->width;
                g->v0 = y1 / (float)font->height;
                g->u1 = x2 / (float)font->width;
                g->v1 = y2 / (float)font->height;
                g->half_width = (x2 - x1) * 0.5f;
                g->half_height = (y2 - y1) * 0.5f;
            }
        }
    }
    config_free(&cfg);
    return true;
}

float font_text_width(const Font *font, const char *text)
{
    float width = 0.0f;

    for (; *text != '\0'; text++)
        width += font->glyphs[(unsigned char)*text].half_width * 2.0f;
    return width;
}

static float line_start(float anchor, float width, float scale, int align)
{
    if (align & FONT_CENTER)
        return anchor - width * scale * 0.5f;
    if (align & FONT_RIGHT)
        return anchor - width * scale;
    return anchor;
}

static int add_glyph(const FontGlyph *g, float cx, float cy, float scale, XVertex *out)
{
    float hw = g->half_width * scale, hh = g->half_height * scale;
    float corners[6][4] = {
        { cx - hw, cy + hh, g->u0, g->v0 }, { cx + hw, cy + hh, g->u1, g->v0 },
        { cx + hw, cy - hh, g->u1, g->v1 }, { cx - hw, cy + hh, g->u0, g->v0 },
        { cx + hw, cy - hh, g->u1, g->v1 }, { cx - hw, cy - hh, g->u0, g->v1 },
    };
    int i;

    for (i = 0; i < 6; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].pos[0] = corners[i][0];
        out[i].pos[1] = corners[i][1];
        out[i].normal[2] = -1.0f;
        out[i].uv[0] = corners[i][2];
        out[i].uv[1] = corners[i][3];
    }
    return 6;
}

/*
 * 0x4271f0. Bytes 0xC0-0xD9 are layout codes rather than letters: they
 * shrink or grow the text (and start the line again), or nudge it left,
 * right, up or down; '|' starts a new line. Each still takes up the room
 * of its own glyph, drawn as a space.
 */
int font_layout(const Font *font, float x, float y, float scale, int align, const char *text,
                XVertex *out, int max)
{
    static const float shrink[5] = { -0.025f, -0.05f, -0.1f, -0.2f, 0.025f };
    static const float grow[3] = { 0.05f, 0.1f, 0.2f };
    static const float nudge[8] = { -8, -16, -32, -64, 8, 16, 32, 64 };
    static const float lift[8] = { 2, 4, 8, 16, -2, -4, -8, -16 };
    float width = font_text_width(font, text);
    float start = line_start(x, width, scale, align), at = start;
    int n = 0;

    for (; *text != '\0'; text++) {
        unsigned char c = (unsigned char)*text;
        const FontGlyph *g = &font->glyphs[c];
        bool space = true;
        float cx;

        if (c >= 0xc0 && c <= 0xc4) {
            scale += shrink[c - 0xc0];
            at = line_start(x, width, scale, align);
        } else if (c >= 0xc5 && c <= 0xc7) {
            scale += grow[c - 0xc5];
            at = line_start(x, width, scale, align);
        } else if (c >= 0xc8 && c <= 0xcf) {
            at += nudge[c - 0xc8];
        } else if (c >= 0xd0 && c <= 0xd7) {
            y += lift[c - 0xd0];
        } else if (c == 0xd8) {
            at += 4.0f;
        } else if (c == 0xd9) {
            at += 2.0f;
        } else if (c == '|') {
            at = start - 2.0f * scale * g->half_width;
            y -= 2.0f * scale * g->half_height;
        } else {
            space = c == ' ';
        }
        cx = at + scale * g->half_width;
        if (!space && n + 6 <= max)
            n += add_glyph(g, cx, y, scale, out + n);
        at = cx + scale * g->half_width;
    }
    return n;
}
