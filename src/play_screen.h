/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * play_screen.h - playing a level.
 *
 *   mouse               move the bat; left button launches the ball
 *   right drag, wheel   turn and zoom the camera, as in the original
 *   Space               reset the camera
 *   F6                  next tune, as in the game
 *   F7 / F8             random background / board floor, as in the game
 *   Left / Right        previous / next level (testing)
 *   Page Up / Down      previous / next level pack (testing)
 *
 * The menus start a game; the play screen says when it is over.
 */
#ifndef OPENMB2_PLAY_SCREEN_H
#define OPENMB2_PLAY_SCREEN_H

#include <SDL.h>
#include <stdbool.h>

typedef struct PlayScreenOptions {
    float mouse_sensitivity; /* from params.txt */
    bool autoplay;           /* the bat follows the ball, for testing */
    bool infravision;        /* from params.txt: show balls through walls */
    bool intro;              /* fly the camera in at the start of a level */
    bool smooth;             /* draw moving things between game steps (OpenMB2's own) */
    bool sort_glass;         /* draw see-through bricks back to front (OpenMB2's own) */
    bool edge_bounce;        /* a bat without end bounces bounces in the middle (OpenMB2's own) */
    bool precise_mouse;        /* finer, unrounded bat moves (OpenMB2's own) */
} PlayScreenOptions;

typedef enum PlayScreenResult {
    PLAY_SCREEN_PLAYING,
    PLAY_SCREEN_GAME_OVER,  /* the last life lost, and the light gone */
    PLAY_SCREEN_PACK_DONE   /* the pack's last level finished */
} PlayScreenResult;

/* Finds the level packs; nothing is played until play_screen_start(). */
bool play_screen_init(SDL_Window *window, const PlayScreenOptions *options);
void play_screen_shutdown(void);

/* The pack folders ("Levels", "Levels0r", ...), sorted by name. */
int play_screen_pack_count(void);
const char *play_screen_pack_folder(int pack);

/* A new game from this level of a pack (from 0), with the bat a pad file
   describes (NULL for each level's own). */
bool play_screen_start(const char *folder, int level, const char *pad);
/* The same from a level file, e.g. "Levels1/001_Vesi.mb2". */
bool play_screen_start_file(const char *path);
/* Leave the level, for the menus. */
void play_screen_stop(void);
int play_screen_score(void);
/* The options page's settings. */
void play_screen_set_options(float mouse_sensitivity, bool infravision);
/* Load the HUD's font again, for the language now in use (see lang_font). */
void play_screen_load_font(void);
void play_screen_handle_event(const SDL_Event *event);
/* Escape: pauses the game, or returns false when it should quit instead. */
bool play_screen_escape(void);
/* Pause if a level is being played (e.g. when the window loses focus). */
void play_screen_pause(void);
bool play_screen_paused(void);

/* Advance the game by ms and draw it. */
PlayScreenResult play_screen_frame(int width, int height, uint32_t ms);

#endif
