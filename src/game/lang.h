/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * lang.h - the game's text in the chosen language (Languages/<name>.txt).
 *
 * A language file is one string per line after a first "/// SYSTEM ///"
 * line; the game refers to them by number (12 is "Score %d"). The bytes
 * are the font's own codes (Windows-1252 for the shipped languages), so
 * they are kept as they are.
 */
#ifndef OPENMB2_LANG_H
#define OPENMB2_LANG_H

#include <stdbool.h>

enum {
    LANG_SCORE = 12,
    LANG_LIVES = 13,
    LANG_LEVEL = 14
};

/*
 * Load the language at this place in the Languages folder, sorted by name
 * (params.txt's "language"), or the first one if there are fewer.
 */
bool lang_load(int index);
void lang_free(void);

/* String number index, or fallback if the file doesn't have it. */
const char *lang_text(int index, const char *fallback);

#endif
