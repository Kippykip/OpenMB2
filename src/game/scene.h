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
#include "../core/xfile.h"

/* All bricks drawn with one texture script. */
typedef struct BrickBatch {
    int texture; /* index into the texture set */
    XVertex *vertices;
    int vertex_count, capacity;
} BrickBatch;

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
    BrickBatch *batches;
    int batch_count;
    int brick_count;
    int other_count; /* objects that aren't bricks, not drawn yet */
} Scene;

/* What randomizetexture does: pick new board floor and background images. */
void scene_randomize_board(Scene *scene, int random);
void scene_randomize_background(Scene *scene, int random);

/* random picks the randomizetexture images, as rand() does in the original. */
bool scene_load(Scene *scene, const char *level_path, int random);
void scene_free(Scene *scene);

#endif
