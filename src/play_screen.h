/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * play_screen.h - playing a level: the game's main screen until the menus
 * arrive.
 *
 *   mouse               move the bat; left button launches the ball
 *   right drag, wheel   turn and zoom the camera, as in the original
 *   Space               reset the camera
 *   F6                  next tune, as in the game
 *   F7 / F8             random background / board floor, as in the game
 *   Left / Right        previous / next level (testing)
 *   Page Up / Down      previous / next level pack (testing)
 *
 * The score and lives show in the window title for now.
 */
#ifndef OPENMB2_PLAY_SCREEN_H
#define OPENMB2_PLAY_SCREEN_H

#include <SDL.h>
#include <stdbool.h>

typedef struct PlayScreenOptions {
    const char *start_level; /* NULL = first level of the first pack */
    float mouse_sensitivity; /* from params.txt */
    bool autoplay;           /* the bat follows the ball, for testing */
    bool infravision;        /* from params.txt: show balls through walls */
    bool intro;              /* fly the camera in at the start of a level */
    bool smooth;             /* draw moving things between game steps (OpenMB2's own) */
} PlayScreenOptions;

bool play_screen_init(SDL_Window *window, const PlayScreenOptions *options);
void play_screen_shutdown(void);
void play_screen_handle_event(const SDL_Event *event);

/* Advance the game by ms and draw it. */
void play_screen_frame(int width, int height, uint32_t ms);

#endif
