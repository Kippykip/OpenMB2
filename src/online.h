/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * online.h - the online high scores, spoken the way the original game
 * speaks them so the same server keeps working for both.
 *
 * Every request is a POST to <server>/scripts/<name>.php whose body is a
 * "key=value&..." line encrypted with Triple DES (see des3.h). The game
 * first asks mb2activation.php until it answers "OK", reads a pack's
 * table from mb2getscore.php as a list of <rec/> and <mes/> lines, and
 * sends a score of a million or more to mb2addscore.php. The original
 * talked to www.dreamdale.com, long gone; the default server is the
 * community's revival of it.
 *
 * Requests run on a thread of their own, so the menus never wait.
 */
#ifndef OPENMB2_ONLINE_H
#define OPENMB2_ONLINE_H

#include <stdbool.h>
#include <stdint.h>

#define ONLINE_DEFAULT_SERVER "https://kippykip.com/mb2s"

typedef enum OnlineState {
    ONLINE_IDLE,     /* nothing asked for yet */
    ONLINE_LOADING,  /* the table is on its way */
    ONLINE_READY,    /* online_rows() has it */
    ONLINE_FAILED    /* no answer, or online scores are off */
} OnlineState;

/* One line of the table. */
typedef struct OnlineRow {
    bool message;        /* a <mes/> line rather than a <rec/> score */
    bool flagged;        /* "a": a score blinks; either slows the scroll */
    char name[64];       /* n */
    int score;           /* s */
    int bat;             /* b: 0 two fans, 1 one, 2 none */
    char date[32];       /* d */
    char time[32];       /* t */
    char level[16];      /* l: "101" for a finished pack */
    char text[256];      /* m, a message's text */
    uint32_t color;      /* c, a message's ARGB colour */
    float scale;         /* z, a message's size */
    char url[256];       /* i, opened when the message is clicked */
} OnlineRow;

/*
 * Set up once: the server ("" turns online scores off), the build name
 * from logos.txt, the graphics card's name, and an id to fall back on
 * where the original's (the C: drive's serial number) can't be had.
 */
void online_init(const char *server, const char *gpu, const char *fallback_id);
void online_shutdown(void);
void online_set_build(const char *build);

/* Start reading a pack's table (the pack number from online_pack_number). */
void online_fetch(int pack);
OnlineState online_state(void);
/* The table read last; valid until the next online_fetch. */
int online_rows(const OnlineRow **rows);

/* Send a score; the original sends only those of a million or more. */
/* With cheats on, scores are still shown but never sent. */
void online_block_sends(bool block);
void online_send(const char *name, int score, int level_score, int level, int pack, int bat);

/* The original's numbers for the level folders: Levels 0, Levels1 to
   Levels7 1 to 7, Levels0r 8; -1 for any other. */
int online_pack_number(const char *folder);
/* The original's numbers for the bats: Pads/common.txt 0, onefan 1,
   nofan 2; 0 for any other. */
int online_bat_number(const char *pad);

#endif
