/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * audio.h - sound effects through SDL audio.
 *
 * The game plays a sound by file name ("ball1.ogg"), looked up in Sounds/,
 * with a volume and a stereo position, the way its sound object is called:
 *
 *     volume  0-100, or -1 for full volume
 *     pan     -100 (left) to 100 (right)
 *
 * Sounds are decoded once with stb_vorbis and kept.
 *
 * Music is one tracker module (Music/mb01.mo3) holding every tune; a
 * Music/<tune>.txt file names the module and the order and row a tune starts
 * at. libopenmpt plays it, looping for ever.
 */
#ifndef OPENMB2_AUDIO_H
#define OPENMB2_AUDIO_H

#include <stdbool.h>

/* The player's settings from params.txt, 0-100. */
bool audio_init(int sound_volume, int music_volume);
void audio_shutdown(void);

void audio_play(const char *name, int volume, int pan);

/* Start the tune a Music/<tune>.txt file describes, e.g. "Music/mb2.txt". */
void audio_play_music(const char *path);
void audio_stop_music(void);

#endif
