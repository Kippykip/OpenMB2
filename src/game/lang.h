/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * lang.h - the game's text in the chosen language (Languages/<name>.txt).
 *
 * A language file is one string per line, numbered from 0 (the first
 * line, "/// SYSTEM ///"). A section line such as "/// PRIZES ///" moves
 * the numbering on to a fixed place (0x418950), so the prize names are
 * always 0x80 onwards. The bytes are the font's own codes (Windows-1252
 * for the shipped languages), so they are kept as they are.
 */
#ifndef OPENMB2_LANG_H
#define OPENMB2_LANG_H

#include <stdbool.h>
#include <stddef.h>

enum {
    LANG_SCORE = 13,
    LANG_LIVES = 14,
    LANG_LEVEL = 15,
    LANG_PAUSE = 16,           /* then 17-19, the lines under it */
    LANG_HYPNOTIC_OVER = 20,
    LANG_YOYO_OVER = 21,
    LANG_FIRE_HINT = 24,       /* two lines */
    LANG_WATER_HINT = 26,      /* two lines */
    LANG_PRIZES = 0x80         /* + PrizeKind */
};

/*
 * Load the language at this place in the Languages folder, sorted by name
 * (params.txt's "language"), or the first one if there are fewer.
 */
bool lang_load(int index);
void lang_free(void);

/* How many languages there are, and one's name ("English"), for the options page. */
int lang_count(void);
bool lang_name(int index, char *out, size_t size);

/* String number index, or fallback if the file doesn't have it. */
const char *lang_text(int index, const char *fallback);

#endif
