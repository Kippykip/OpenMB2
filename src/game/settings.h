/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * settings.h - OpenMB2's own settings, in openmb2.ini next to params.txt.
 *
 * The original game never reads this file, so anything it couldn't
 * understand goes here: any screen size, and the switches for what
 * OpenMB2 does differently. params.txt keeps the settings both games
 * share. A switch on the command line wins over the file for that run.
 *
 *   [display]
 *   width = 1920        ; 0 x 0: params.txt's video mode
 *   height = 1080
 *   maximized = off     ; the window was maximised when OpenMB2 closed
 *   fullscreen = auto   ; on, off, or auto: params.txt's window setting
 *   vsync = on
 *
 *   [options]
 *   smooth = on         ; --no-smooth
 *   level_intro = on    ; --no-intro
 *   sort_glass = on     ; --original-glass
 *   edge_bounce = on    ; --original-bounce
 *   precise_mouse = on    ; --original-mouse
 *   wide_menus = off    ; --wide-menus
 *   language =          ; one only in OpenMB2\Languages, or params.txt's
 */
#ifndef OPENMB2_SETTINGS_H
#define OPENMB2_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum SettingsFullscreen {
    FULLSCREEN_AUTO,
    FULLSCREEN_ON,
    FULLSCREEN_OFF
} SettingsFullscreen;

typedef struct Settings {
    int width, height; /* 0 for params.txt's video mode */
    bool maximized;
    SettingsFullscreen fullscreen;
    bool vsync;
    bool smooth;
    bool level_intro;
    bool sort_glass;
    bool edge_bounce;
    bool precise_mouse;
    bool wide_menus;
    char language[64]; /* a language only OpenMB2 has, "" for params.txt's */
} Settings;

void settings_set_defaults(Settings *settings);
/* Read the file's text over the defaults; unknown lines are skipped. */
void settings_parse(Settings *settings, const char *text, size_t size);
/* The file's text, with comments; returns its length. */
int settings_format(const Settings *settings, char *out, size_t size);

#endif
