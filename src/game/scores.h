/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * scores.h - the high score table in score.dat (0x4205e0, 0x4203a0).
 *
 *     int 2                    the format
 *     int                      every score added up, as a check
 *     30 times:
 *       int n, then n bytes    the name, byte i stored as name[i] ^ (0x63 + i)
 *       int                    the score
 *
 * Empty places have no name and a score of 0. Nothing here touches files.
 */
#ifndef OPENMB2_SCORES_H
#define OPENMB2_SCORES_H

#include <stdbool.h>
#include <stddef.h>

#define SCORES_PLACES 30
#define SCORES_NAME_SIZE 260

typedef struct Scores {
    char names[SCORES_PLACES][SCORES_NAME_SIZE];
    int scores[SCORES_PLACES];
} Scores;

bool scores_decode(Scores *scores, const unsigned char *data, size_t size);
/* The file's bytes; free() the result. */
unsigned char *scores_encode(const Scores *scores, size_t *size);

/* How many places hold a score. */
int scores_count(const Scores *scores);

/* Whether a score earns a place: more than the last place's (0x417c60). */
bool scores_qualifies(const Scores *scores, int score);

/* Put a name and score in its place, moving the ones below down (0x420280);
   returns the place, or -1 if it didn't earn one. */
int scores_add(Scores *scores, const char *name, int score);

#endif
