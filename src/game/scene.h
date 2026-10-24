/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * scene.h - everything needed to draw a level, built on the CPU.
 *
 * Loading a level pulls in its texture set, board, background and brick
 * models. Bricks are baked the way the original does it: each brick's
 * model is rotated, moved into place and given its tile's UVs, then all
 * bricks that share a texture go into one vertex list, so a whole level
 * draws in a handful of calls.
 */
#ifndef OPENMB2_SCENE_H
#define OPENMB2_SCENE_H

#include "board.h"
#include "level.h"
#include "textures.h"
#include "../core/matrix.h"
#include "../core/xfile.h"

/* All bricks drawn with one texture script. */
typedef struct BrickBatch {
    int texture; /* index into the texture set */
    XVertex *vertices;
    int vertex_count, capacity;
} BrickBatch;

/*
 * One brick from the level, decoded (see scene.c). Gameplay hides bricks
 * and removes them; scene_bake_bricks() then rebuilds the batches.
 */
/*
 * Fire and fountains (types 0x64 / 0x90 and 0x68 / 0x94): animated
 * pictures from billboards.png standing in the grid like bricks
 * (0x405cc0, 0x405e50).
 */
typedef struct SceneBillboard {
    const LevelObject *object;
    Vec3 position;  /* (x - 0.01, y, z), as for bricks */
    bool water;     /* a fountain; otherwise fire */
} SceneBillboard;

/*
 * Animals (and people, aliens and the UFO), types 0xb8 to 0xde: a skinned
 * model standing in the grid like a brick, with a sound when it is hit and
 * one it makes now and then (factory 0x412180, class 0x407cd0). The
 * firework rocket, type 0xa4 (class 0x407940), and the teleporters, 0x84
 * and 0x88 (0x408710, 0x408760), stand the same way.
 */
typedef enum SceneModelKind {
    MODEL_ANIMAL,
    MODEL_ROCKET,
    MODEL_TELEPORT,      /* sends a ball on to the cell in its record */
    MODEL_TELEPORT_END,  /* where it arrives: only for show */
    MODEL_TRAIN          /* runs round an ellipse, out of the grid (0x408900) */
} SceneModelKind;

typedef struct SceneAnimal {
    int type;
    const char *model;      /* in SkinMeshes */
    const char *hit_sound;
    const char *idle_sound;
    SceneModelKind kind;
} SceneAnimal;

/* The animal, rocket or teleporter an object type is, or NULL. */
const SceneAnimal *scene_animal(int type);

typedef struct SceneBrick {
    const LevelObject *object;
    Vec3 position; /* where the original places it: (x - 0.01, y, z) */
    uint8_t flags, rotation;
    int model, height, texture, tile, overlay_tile;
    bool hidden;   /* not drawn, e.g. glass that hasn't been hit */
    bool removed;  /* broken */
    /*
     * Tipping off a ledge: how far it still has to turn, in radians (+ to
     * the left, - to the right). It turns about its bottom edge's middle.
     */
    float tip;
    float roll;    /* turns from earlier tips, about its centre: it stays tipped over */
    /* A broken brick shrinks away for a while before it's gone. */
    uint32_t dying_ms; /* time left, 0 = not dying */
    float scale;       /* about its centre; 1 normally */
    /* Hits taken by a strong brick: each moves its texture down one tile,
       to the next crack picture (0x4033c0). */
    int cracks;
    /* Colour-changing and explosive bricks: rows their texture has moved on
       so far, wrapping round the sheet (0x407880). */
    int flash;
    /* Stone (type 0x30) shines through its texture's rows once each time a
       hit doesn't break it (0x4083b0, 0x408300); ms into the shine. */
    bool shining;
    uint32_t shine_ms;
    /* An animal: what it is, its animation's start (rand() % 5000 ms), and
       once hit, how long it has been flying off and how far it has rolled. */
    const SceneAnimal *animal;
    uint32_t animation_ms;
    uint32_t flying_ms;   /* 0 = not flying */
    float flip;
    /* A train: how far round it has run, in radians, and once it has
       rolled over, how far it has sunk. */
    float heading, drop;
} SceneBrick;

/*
 * A crumb: a small copy of a broken brick that flies off spinning,
 * bounces on the floor and shrinks away.
 */
typedef struct SceneCrumb {
    int brick;          /* the brick it is a copy of */
    Vec3 position, velocity, bounce;
    Vec3 axis;          /* it spins about this... */
    float spin, angle;  /* ...this much a frame; turned so far */
    float scale;
    uint32_t age;
} SceneCrumb;

/* The flash where a brick broke: 16 frames from explose.png. */
typedef struct SceneExplosion {
    Vec3 position;
    uint32_t age;
} SceneExplosion;

#define SCENE_MAX_CRUMBS 256
#define SCENE_MAX_EXPLOSIONS 64

typedef struct ModelCache ModelCache;

typedef struct Scene {
    Level level;
    TextureSet textures;
    Board board;
    Background background;
    XModel board_model;
    XModel background_model;
    /* From randomizetexture: the board floor's ("Doska") and the background's image. */
    char board_texture[128];
    char background_texture[128];
    /* Height the board model is moved by so its floor top sits at y = -1. */
    float board_y;
    SceneBrick *bricks;
    int brick_count;
    SceneBillboard *billboards;
    int billboard_count;
    int other_count; /* objects that aren't bricks, not drawn yet */
    BrickBatch *batches;
    int batch_count;
    ModelCache *models; /* brick models, kept for rebaking */
    SceneCrumb crumbs[SCENE_MAX_CRUMBS];
    int crumb_count;
    SceneExplosion explosions[SCENE_MAX_EXPLOSIONS];
    int explosion_count;
} Scene;

/* What randomizetexture does: pick new board floor and background images. */
void scene_randomize_board(Scene *scene, int random);
void scene_randomize_background(Scene *scene, int random);

/* Rebuild the brick batches from the bricks that are showing and the crumbs. */
void scene_bake_bricks(Scene *scene);
/* Where a brick's model goes: its rotations, roll, shrink and tip, then its place. */
Mat4 scene_brick_matrix(const SceneBrick *b);

/* A brick has broken: it starts shrinking, and crumbs and a flash fly off. */
/* Start an explosion flash (the factory's object 0x40). */
void scene_add_explosion(Scene *scene, Vec3 position);
void scene_break_brick(Scene *scene, int index);

/* Move the breaking effects on by one game step. True if anything moved. */
bool scene_update_effects(Scene *scene, uint32_t ms);

/* random picks the randomizetexture images, as rand() does in the original. */
bool scene_load(Scene *scene, const char *level_path, int random);
void scene_free(Scene *scene);

#endif
