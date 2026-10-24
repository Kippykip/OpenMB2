/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * save.h - the original's saved games (default.save, slot1-3.save) and
 * level progress files (s<pack folder>.sav), byte for byte, so either
 * game can load the other's.
 *
 * Both are written by the original with fopen "w", in text mode, so every
 * 0x0A byte went to disk as 0D 0A; save_encode() does the same and
 * save_decode() undoes it. A saved game holds the score and lives as they
 * were when the level started, so loading starts that level again:
 *
 *     0x000  "Magic Ball 2 saved game \n" and a 0
 *     0x01C  0
 *     0x020  score at the start of the level
 *     0x024  lives at the start of the level
 *     0x028  level, from 0, in the pack's list
 *     0x02C  0 (a variable nothing else uses)
 *     0x030  0
 *     0x034  the pack's folder, 260 bytes
 *     0x138  the pack's level list (0x40afb0): the number of levels, the
 *            progress file's name (260 bytes), then 256 times a file
 *            name (260 bytes) and its progress (3 ints)
 *
 * A progress file is just the 256 progress entries. The first int of
 * each is 1 once the level is open to play (finishing a level opens the
 * next, 0x4197f1); the other two are always 0.
 */
#ifndef OPENMB2_SAVE_H
#define OPENMB2_SAVE_H

#include <stdbool.h>
#include <stddef.h>

#define SAVE_MAX_LEVELS 256
#define SAVE_NAME_SIZE 260

typedef struct SaveGame {
    int score, lives, level;
    char folder[SAVE_NAME_SIZE];
    int level_count;
    char progress_file[SAVE_NAME_SIZE];
    char names[SAVE_MAX_LEVELS][SAVE_NAME_SIZE];
    int progress[SAVE_MAX_LEVELS][3];
} SaveGame;

/* The file's bytes, as the original writes them; free() the result. */
unsigned char *save_encode(const SaveGame *save, size_t *size);
bool save_decode(const unsigned char *data, size_t size, SaveGame *save);

/* A progress file: fills progress, returns false if it's not one. */
bool save_decode_progress(const unsigned char *data, size_t size, int progress[SAVE_MAX_LEVELS][3]);
/* The file's bytes; free() the result. */
unsigned char *save_encode_progress(const int progress[SAVE_MAX_LEVELS][3], size_t *size);

#endif
