/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * menu_screen.h - everything outside a level, as the original does it:
 * splash.png fading out, the Alawar intro, then the main menu and its
 * pages (the Menu folder, see game/menu.h), the high scores and entering a
 * name after a game.
 *
 * The mouse points and the left button clicks; Escape goes back to the
 * main menu, and any key or button skips the intro.
 */
#ifndef OPENMB2_MENU_SCREEN_H
#define OPENMB2_MENU_SCREEN_H

#include "game/params.h"
#include "game/settings.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum MenuAction {
    MENU_ACTION_NONE,
    MENU_ACTION_PLAY,  /* start the game menu_screen_game() describes */
    MENU_ACTION_QUIT
} MenuAction;

/* params is the player's settings, which the options page changes. */
bool menu_screen_init(SDL_Window *window, Params *params, bool intro);
void menu_screen_shutdown(void);

/* Start with the splash screen (and the intro after it). */
void menu_screen_show_splash(void);
/* A page from its list file, e.g. "Menu/loptions.txt" (for testing). */
void menu_screen_show_page(const char *list);
/* The main menu, as Escape from the game leaves for it (state 0x40). */
void menu_screen_show_main(void);
/* After the last life: the name for the high scores, or the main menu. */
void menu_screen_show_game_over(int score);
/* After the last level of a pack: Menu/lendgame.txt. */
void menu_screen_show_end_of_pack(int score);

void menu_screen_handle_event(const SDL_Event *event);
MenuAction menu_screen_frame(int width, int height, uint32_t ms);

/* Fill a screen of another shape instead of keeping 4:3 between black bars. */
void menu_screen_set_wide(bool wide);

/*
 * openmb2.ini's settings: the options page's video mode offers every size
 * the display has and keeps the one picked there (params.txt's resolution
 * too, when it is one of the original's), then calls changed().
 */
void menu_screen_set_settings(Settings *settings, void (*changed)(void));

/* What MENU_ACTION_PLAY plays: a pack folder, a level in it, the bat's pad file. */
void menu_screen_game(const char **folder, int *level, const char **pad);

#endif
