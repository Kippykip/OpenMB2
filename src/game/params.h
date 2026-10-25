/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * params.h - the player's settings, stored in params.txt in the game folder.
 *
 * The original params.txt looks like this:
 *
 *   {
 *     soundvolume: 100
 *     musicvolume: 100
 *     window: 1
 *     language: 0
 *     resolution: 0
 *     hallfame: 0
 *     infravision: 1
 *     act: 0
 *     joystick: 0
 *     msensivity: 0.750000
 *   }
 *
 * We read the same file so the player's existing settings carry over.
 * "act" (online activation) and "hallfame" are kept only so they can be
 * written back unchanged.
 */
#ifndef OPENMB2_PARAMS_H
#define OPENMB2_PARAMS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Params {
    int sound_volume;        /* 0-100 */
    int music_volume;        /* 0-100 */
    bool windowed;
    int language;            /* index into the Languages folder, alphabetical */
    int resolution;          /* index into the original game's mode list */
    bool hall_of_fame;
    bool infravision;        /* draw balls through bricks */
    bool activated;
    bool joystick;
    float mouse_sensitivity;
} Params;

/* The original's video modes, which params.txt's resolution counts. */
#define PARAMS_VIDEO_MODES 6
/* A mode's size; an index out of range gives the first. */
void params_video_mode(int index, int *width, int *height);
/* The index of the mode of this size, or -1. */
int params_find_video_mode(int width, int height);

/* Fill params with the original game's defaults. */
void params_set_defaults(Params *params);

/* Load params.txt from the game folder. Missing keys keep their defaults. */
bool params_load(Params *params, const char *game_path);

/* The file's text, as the original writes it (0x4178e0); returns its length. */
int params_format(const Params *params, char *out, size_t size);

#endif
