/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * play.h - the rules of the game: bat, balls, bricks, score and lives.
 *
 * This is the original's logic, worked out from its exe (docs/gameplay.md
 * has the details and where each rule came from). It knows nothing about
 * drawing or sound devices: it changes the scene's bricks, and reports
 * sounds through a callback.
 *
 * Time moves in steps, as in the original: play_update() runs a step once
 * more than 17 ms have built up, and speeds are in units per 10 ms.
 */
#ifndef OPENMB2_PLAY_H
#define OPENMB2_PLAY_H

#include "scene.h"

#include <stdbool.h>
#include <stdint.h>

#define PLAY_MAX_BALLS 32

/* Ball flags, with the original's values. */
enum {
    BALL_STUCK = 0x02,     /* waiting on the bat */
    BALL_POWER = 0x10,     /* goes through bricks */
    BALL_EXPLOSIVE = 0x20, /* blows bricks up */
    BALL_LOST = 0x8000
};

typedef struct Ball {
    Vec3 position, previous, velocity;
    uint32_t flags;
    float stuck_offset;   /* x from the bat's centre while stuck */
    int round_bounces;    /* bounces in a row off a round wall */
} Ball;

typedef struct Bat {
    Vec3 position;
    float half_width;
    int tilt;             /* the model leans while it moves, ±299 */
} Bat;

/* Per brick of the scene, the gameplay side. */
typedef struct PlayBrick {
    int hits_left;
    int points;           /* score / 100 */
    bool counts;          /* must be broken to finish the level */
    int cell_x, cell_y, cell_z; /* where it is in the grid; the drawn position catches up */
    float target_x, target_y;   /* where it is falling or tipping to */
} PlayBrick;

typedef enum PlayState {
    PLAY_RUNNING,
    PLAY_BAT_LOST,        /* no balls left; a new one comes after a pause */
    PLAY_LEVEL_DONE,
    PLAY_GAME_OVER
} PlayState;

typedef void (*PlaySoundFn)(const char *name, int volume, int pan, void *user);

typedef struct Play {
    Scene *scene;
    PlayBrick *bricks;           /* one per scene brick */
    int16_t grid[34][15][18];    /* scene brick index + 1, 0 = empty */
    Ball balls[PLAY_MAX_BALLS];
    int ball_count;
    Bat bat;
    int score, lives;
    int bricks_left;
    PlayState state;
    uint32_t state_time;         /* ms in the current state */
    uint32_t time;               /* ms since the level started */
    float mouse_sensitivity;
    uint32_t pending_ms;         /* time not yet turned into a step */
    bool bricks_changed;         /* the scene's bricks need rebaking */
    PlaySoundFn sound;
    void *sound_user;
} Play;

/* Start a level. score and lives carry over between levels. */
bool play_start(Play *play, Scene *scene, int score, int lives, float mouse_sensitivity);
void play_free(Play *play);

/* Mouse movement in pixels, and the left button: releases the ball. */
void play_mouse_move(Play *play, int dx);
void play_click(Play *play);

/* Let time pass (in ms). */
void play_update(Play *play, uint32_t ms);

/* For testing: the bat follows the first ball and launches it. Call before play_update(). */
void play_autopilot(Play *play);

#endif
