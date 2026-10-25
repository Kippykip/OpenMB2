/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void settings_set_defaults(Settings *settings)
{
    settings->width = 0;
    settings->height = 0;
    settings->maximized = false;
    settings->fullscreen = FULLSCREEN_AUTO;
    settings->vsync = true;
    settings->smooth = true;
    settings->level_intro = true;
    settings->sort_glass = true;
    settings->edge_bounce = true;
    settings->precise_mouse = true;
    settings->wide_menus = false;
    settings->language[0] = '\0';
    snprintf(settings->score_server, sizeof(settings->score_server), "https://kippykip.com/mb2s");
    settings->online_id[0] = '\0';
}

static bool same(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

/* on/off, yes/no, true/false or a number; anything else keeps the old value. */
static bool parse_bool(const char *value, bool old)
{
    if (same(value, "on") || same(value, "yes") || same(value, "true"))
        return true;
    if (same(value, "off") || same(value, "no") || same(value, "false"))
        return false;
    if (isdigit((unsigned char)value[0]))
        return atoi(value) != 0;
    return old;
}

static void set(Settings *s, const char *section, const char *key, const char *value)
{
    if (same(section, "display")) {
        if (same(key, "width"))
            s->width = atoi(value) > 0 ? atoi(value) : 0;
        else if (same(key, "height"))
            s->height = atoi(value) > 0 ? atoi(value) : 0;
        else if (same(key, "maximized"))
            s->maximized = parse_bool(value, s->maximized);
        else if (same(key, "fullscreen"))
            s->fullscreen = same(value, "auto") ? FULLSCREEN_AUTO
                            : parse_bool(value, false) ? FULLSCREEN_ON
                                                       : FULLSCREEN_OFF;
        else if (same(key, "vsync"))
            s->vsync = parse_bool(value, s->vsync);
    } else if (same(section, "options")) {
        if (same(key, "smooth"))
            s->smooth = parse_bool(value, s->smooth);
        else if (same(key, "level_intro"))
            s->level_intro = parse_bool(value, s->level_intro);
        else if (same(key, "sort_glass"))
            s->sort_glass = parse_bool(value, s->sort_glass);
        else if (same(key, "edge_bounce"))
            s->edge_bounce = parse_bool(value, s->edge_bounce);
        else if (same(key, "precise_mouse"))
            s->precise_mouse = parse_bool(value, s->precise_mouse);
        else if (same(key, "wide_menus"))
            s->wide_menus = parse_bool(value, s->wide_menus);
        else if (same(key, "language"))
            snprintf(s->language, sizeof(s->language), "%s", value);
    } else if (same(section, "online")) {
        if (same(key, "score_server"))
            snprintf(s->score_server, sizeof(s->score_server), "%s", value);
        else if (same(key, "id"))
            snprintf(s->online_id, sizeof(s->online_id), "%s", value);
    }
}

/* Cut the spaces off both ends of a string, in place. */
static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

void settings_parse(Settings *settings, const char *text, size_t size)
{
    char section[32] = "", line[256];
    size_t at = 0;

    while (at < size) {
        size_t n = 0;
        char *s, *equals, *comment;

        while (at < size && text[at] != '\n') {
            if (n + 1 < sizeof(line))
                line[n++] = text[at];
            at++;
        }
        at++;
        line[n] = '\0';
        if ((comment = strpbrk(line, ";#")) != NULL)
            *comment = '\0';
        s = trim(line);
        if (s[0] == '[') {
            char *close = strchr(s, ']');
            if (close != NULL) {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", trim(s + 1));
            }
        } else if ((equals = strchr(s, '=')) != NULL) {
            *equals = '\0';
            set(settings, section, trim(s), trim(equals + 1));
        }
    }
}

static const char *on_off(bool on)
{
    return on ? "on" : "off";
}

int settings_format(const Settings *s, char *out, size_t size)
{
    static const char *const fullscreen[] = { "auto", "on", "off" };

    return snprintf(out, size,
                    "; OpenMB2's own settings. The original game doesn't read this file;\n"
                    "; the settings both games share stay in params.txt.\n"
                    "; A switch on the command line wins over this file for that run.\n"
                    "\n"
                    "[display]\n"
                    "; The window's size, or the screen mode when full screen; a window\n"
                    "; resized by hand keeps its size here. 0 x 0 uses params.txt's video\n"
                    "; mode (and the desktop when full screen).\n"
                    "width = %d\n"
                    "height = %d\n"
                    "; The window was maximised when OpenMB2 closed, and opens so again\n"
                    "maximized = %s\n"
                    "; on, off, or auto to follow params.txt's window setting\n"
                    "fullscreen = %s\n"
                    "vsync = %s\n"
                    "\n"
                    "[options]\n"
                    "; Draw moving things between game steps (off: --no-smooth)\n"
                    "smooth = %s\n"
                    "; Fly the camera in at the start of a level (off: --no-intro)\n"
                    "level_intro = %s\n"
                    "; Draw see-through bricks back to front (off: --original-glass)\n"
                    "sort_glass = %s\n"
                    "; Bats without end bounces bounce as in the middle (off: --original-bounce)\n"
                    "edge_bounce = %s\n"
                    "; Finer bat moves, from 1/8 of the original's speed (off: --original-mouse)\n"
                    "precise_mouse = %s\n"
                    "; Fill a wide screen with the menus, not 4:3 (on: --wide-menus)\n"
                    "wide_menus = %s\n"
                    "; A language only in the OpenMB2 folder's Languages; empty uses\n"
                    "; params.txt's, which the original game shares\n"
                    "language = %s\n"
                    "\n"
                    "[online]\n"
                    "; Where the online high scores are kept; the original's server is gone,\n"
                    "; this is its community revival. Empty turns online scores off.\n"
                    "score_server = %s\n"
                    "; Who you are there, where the original's id (from the C: drive) can't be had\n"
                    "id = %s\n",
                    s->width, s->height, on_off(s->maximized), fullscreen[s->fullscreen], on_off(s->vsync),
                    on_off(s->smooth), on_off(s->level_intro), on_off(s->sort_glass),
                    on_off(s->edge_bounce), on_off(s->precise_mouse), on_off(s->wide_menus), s->language, s->score_server,
                    s->online_id);
}
