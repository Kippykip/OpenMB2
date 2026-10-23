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
 * Sounds are decoded once with stb_vorbis and kept. Music comes later.
 */
#ifndef OPENMB2_AUDIO_H
#define OPENMB2_AUDIO_H

#include <stdbool.h>

/* sound_volume is the player's setting from params.txt, 0-100. */
bool audio_init(int sound_volume);
void audio_shutdown(void);

void audio_play(const char *name, int volume, int pan);

#endif
