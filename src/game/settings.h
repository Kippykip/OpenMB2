/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * settings.h - the player's settings, in openmb2.ini next to params.txt.
 *
 * All of OpenMB2's settings live here, the ones the original keeps in
 * params.txt too. The first time, those come from params.txt, which
 * OpenMB2 reads but no longer writes, so the original keeps its own.
 * A switch on the command line wins over the file for that run.
 *
 *   [gameplay]
 *   language = English
 *   mouse_speed = 0.75     ; 0.5 to 1
 *   xray_ball = on
 *   skip_transitions = off ; the camera's flight in at a level's start
 *
 *   [video]
 *   width = 1920           ; 0 x 0: params.txt's video mode
 *   height = 1080
 *   maximized = off        ; the window was maximised when OpenMB2 closed
 *   windowed = on
 *   vsync = on
 *   interpolate = on       ; --no-smooth
 *
 *   [audio]
 *   sound_volume = 100
 *   music_volume = 100
 *
 *   [joystick]
 *   enabled = on
 *   vibration = on
 *   sensitivity = 50       ; 0 to 100
 *
 *   [options]              ; what OpenMB2 does differently
 *   sort_glass = on        ; --original-glass
 *   edge_bounce = on       ; --original-bounce
 *   precise_mouse = on     ; --original-mouse
 *   wide_menus = off       ; --wide-menus
 *
 *   [online]
 *   score_server = https://kippykip.com/mb2s   ; empty: no online scores
 *
 * Files from before keep working: [display] is read as [video], its
 * fullscreen = auto follows params.txt, and [options] smooth, level_intro
 * and language are read too.
 */
#ifndef OPENMB2_SETTINGS_H
#define OPENMB2_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "params.h"

typedef struct Settings {
    /* The settings the original keeps in params.txt. resolution counts
       only while width and height are 0; hall_of_fame and activated
       aren't used. */
    Params params;
    int width, height; /* 0 for params.txt's video mode */
    bool maximized;
    bool vsync;
    bool smooth;
    bool skip_transitions;
    bool joystick_vibration;
    int joystick_sensitivity; /* 0-100 */
    bool sort_glass;
    bool edge_bounce;
    bool precise_mouse;
    bool wide_menus;
    char language[64]; /* by name; "" for params.language */
    char score_server[256]; /* the online scores' server, "" for none */
    char online_id[32];     /* who we are online where the original's id can't be had */
} Settings;

void settings_set_defaults(Settings *settings);
/* params.txt's settings, when there is one, over the defaults; the
   joystick stays on whatever params.txt says. */
void settings_import_params(Settings *settings, const char *game_path);
/* Read the file's text over the defaults; unknown lines are skipped. */
void settings_parse(Settings *settings, const char *text, size_t size);
/* The file's text, with comments; returns its length. */
int settings_format(const Settings *settings, char *out, size_t size);

#endif
