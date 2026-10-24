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

/* A brick has broken: it starts shrinking, and crumbs and a flash fly off. */
void scene_break_brick(Scene *scene, int index);

/* Move the breaking effects on by one game step. True if anything moved. */
bool scene_update_effects(Scene *scene, uint32_t ms);

/* random picks the randomizetexture images, as rand() does in the original. */
bool scene_load(Scene *scene, const char *level_path, int random);
void scene_free(Scene *scene);

#endif
