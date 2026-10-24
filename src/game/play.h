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
#define PLAY_MAX_PRIZES 4   /* the original drops no more while 4 are falling */
#define PLAY_MAX_TRAIL 256
#define PLAY_MAX_BULLETS 64

/* Ball flags, with the original's values. */
enum {
    BALL_STUCK = 0x02,     /* waiting on the bat */
    BALL_YOYO = 0x04,      /* on the rubber bat's string */
    BALL_POWER = 0x10,     /* goes through bricks */
    BALL_EXPLOSIVE = 0x20, /* blows bricks up */
    BALL_HYPNOTIC = 0x40,
    BALL_LOST = 0x8000
};

typedef struct Ball {
    Vec3 position, previous, velocity;
    uint32_t flags;
    float stuck_offset;   /* x from the bat's centre while stuck */
    int round_bounces;    /* bounces in a row off a round wall */
    uint32_t age;         /* ms since it was made */
    uint32_t trail_time;  /* a powerball leaves its next trail spot at this age */
    float spin;           /* turns 0.03 a step; the explosive ball is drawn turned by it */
    float wobble;         /* a hypnotic ball's swing, in radians of a sine */
} Ball;

/* The prizes, numbered as in the original. */
typedef enum PrizeKind {
    PRIZE_NONE,
    PRIZE_INCBAT,
    PRIZE_DECBAT,
    PRIZE_TOPOWERFULL,
    PRIZE_TONORMAL,
    PRIZE_TOSIMPLE,
    PRIZE_TOEXPLOSIVE,
    PRIZE_TOGIPNOZ,
    PRIZE_SPEEDUP,
    PRIZE_SPEEDDOWN,
    PRIZE_TRIPPLEBALL,
    PRIZE_MAGNIT,
    PRIZE_GUN,
    PRIZE_BAZUKA,
    PRIZE_CANNON,
    PRIZE_RUBBER,
    PRIZE_NEXTLEVEL,
    PRIZE_KILLBAT,
    PRIZE_LIFE,
    PRIZE_KINDS
} PrizeKind;

/* What Prizes/<name>.txt says about a kind of prize. */
typedef struct PrizeInfo {
    bool loaded;
    int score;
    char sound[64];
    char model[64];        /* in SkinMeshes */
    float glow_size[2];    /* half width and height of the glow */
    float glow_offset[3];
    uint32_t color;        /* ARGB, the glow's tint */
} PrizeInfo;

/* A prize falling out of a broken brick. */
typedef struct Prize {
    PrizeKind kind;
    Vec3 position, velocity;
    Vec3 bounce;           /* the velocity it leaves the floor with, shrinking */
    float scale;           /* shrinks to nothing once past the bat */
    float glow;            /* the glow's size, likewise */
    uint32_t age;          /* ms, for its animation */
} Prize;

/* A spot of a powerball's or a rocket's trail, fading over 250 ms. */
typedef struct TrailSpot {
    Vec3 position;
    uint32_t color;        /* ARGB */
    float size;
    int life;              /* ms left */
} TrailSpot;

/* What the bat's weapons fire: the pad file names a Bullets/ file for each. */
typedef enum Weapon { WEAPON_GUN, WEAPON_BAZOOKA, WEAPON_CANNON, WEAPON_COUNT } Weapon;

typedef enum BulletKind {
    BULLET_BULLET = 1,     /* breaks or hits the brick it reaches */
    BULLET_ROCKET,         /* breaks every brick in its way */
    BULLET_YADRO,          /* a cannon ball: blows up the brick it reaches */
    BULLET_SMALL_CANNON
} BulletKind;

typedef struct BulletInfo {
    bool loaded;
    BulletKind kind;
    Vec3 velocity, bounce; /* as fired straight ahead */
    float gravity;
    float shadow_size[3];
    uint32_t shadow_color;
    char model[64];
    int delay;             /* ms before the bat can fire again */
} BulletInfo;

typedef struct Bullet {
    Weapon weapon;
    Vec3 position, velocity, bounce;
    uint32_t age;
    uint32_t trail_time;
    float spin;            /* about its own length, 0.01 a ms */
} Bullet;

/* A piece of a bat blown apart: one of the model's frames, flying off. */
#define BAT_PIECES 10
typedef struct BatPiece {
    Vec3 position, velocity; /* from where the bat was */
    Vec3 axis;               /* it spins about this... */
    float angle, spin;       /* ...this far so far, and this fast */
} BatPiece;

/* What the bat is, with the original's values. */
typedef enum BatMode {
    BAT_NORMAL = 0x01,
    BAT_MAGNET = 0x02,
    BAT_GUN = 0x04,
    BAT_BAZOOKA = 0x08,
    BAT_CANNON = 0x10,
    BAT_RUBBER = 0x20,
    BAT_DEAD = 0x40
} BatMode;

typedef struct Bat {
    Vec3 position;
    float half_width;
    float target_width;   /* the half width it is growing or shrinking to */
    BatMode mode;
    int ammo;
    unsigned weapon_time; /* the weapon frames' animation: 0-250 coming out, 250-500 firing */
    uint32_t ready_time;  /* the weapon can fire again at this time */
    uint32_t rubber_end;  /* the rubber bat turns back at this time */
    int tilt;             /* the model leans while it moves, ±299 */
    /*
     * The bounce the model plays when a ball hits it: the time into the
     * model's animation (0 = still), and which bounce (0 middle, 1 right
     * end, 2 left end).
     */
    unsigned bounce_time;
    int bounce_kind;
    unsigned bounce_ms;   /* real time since the hit */
    /* After losing the last ball: the pieces, fading out over this time. */
    BatPiece pieces[BAT_PIECES];
    uint32_t pieces_ms;
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
    Prize prizes[PLAY_MAX_PRIZES];
    int prize_count;
    PrizeInfo prize_info[PRIZE_KINDS];
    uint32_t prize_time;         /* no prize drops before this time */
    PrizeKind last_prize;        /* never the same kind twice running */
    int medal_timer;             /* ms to the next next-level medal */
    TrailSpot trail[PLAY_MAX_TRAIL];
    int trail_count;
    Bullet bullets[PLAY_MAX_BULLETS];
    int bullet_count;
    BulletInfo bullet_info[WEAPON_COUNT];
    uint32_t hypnotic_end;       /* hypnotic balls turn back at this time, 0 = none */
    PlayState state;
    uint32_t state_time;         /* ms in the current state */
    uint32_t time;               /* ms since the level started */
    float mouse_sensitivity;
    uint32_t pending_ms;         /* time not yet turned into a step */
    bool bricks_changed;         /* the scene's bricks need rebaking */
    PlaySoundFn sound;
    void *sound_user;
} Play;

/* Prize names as the files call them ("incbat"), "" for PRIZE_NONE. */
const char *play_prize_name(PrizeKind kind);

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

/* For testing: drop a prize at a point. */
void play_drop_prize(Play *play, PrizeKind kind, Vec3 at);

#endif
