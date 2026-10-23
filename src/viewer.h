/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * viewer.h - the milestone 2 level viewer.
 *
 * Shows any level from any pack the way the game frames it at the start
 * of play, so it can be compared side by side with the original.
 *
 *   Left / Right        previous / next level
 *   Page Up / Down      previous / next level pack
 *   mouse drag, wheel   turn and zoom the camera
 *   Space               reset the camera
 *   F7 / F8             random background / board floor, as in the game
 */
#ifndef OPENMB2_VIEWER_H
#define OPENMB2_VIEWER_H

#include <SDL.h>
#include <stdbool.h>

/* start_level may be NULL (first level of the first pack) or a game path. */
bool viewer_init(SDL_Window *window, const char *start_level);
void viewer_shutdown(void);
void viewer_handle_event(const SDL_Event *event);
void viewer_draw(int width, int height, float time_ms);

#endif
