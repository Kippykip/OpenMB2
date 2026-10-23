/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * board.h - board and background definitions (the files in Boards/ and Backgrounds/).
 *
 *     rect                  shape: rect, round or compaund (sic)
 *     halfwidth: 12.        playing field size, before scaling
 *     halfheight: 22.5
 *     radius: 12.           round and compound boards
 *     batline: -2.5         z of the bat
 *     scalex: 1.3           scales halfwidth and radius
 *     scalez: 1.3           scales halfheight and batline
 *     camera: 11. 44.       look-at z, camera distance
 *     skinmesh: boardrect.x model in SkinMeshes/
 *     randomizetexture      pick a random Board/<Shape>NN.jpg
 *
 * Backgrounds only have "skinmesh" and the optional "randomizetexture",
 * which picks a random Textures/BG/bgNN.jpg.
 */
#ifndef OPENMB2_BOARD_H
#define OPENMB2_BOARD_H

#include <stdbool.h>

/* Values match the shape field in the original's board object. */
typedef enum BoardShape {
    BOARD_RECT = 0,
    BOARD_ROUND = 2,
    BOARD_COMPOUND = 4
} BoardShape;

typedef struct Board {
    BoardShape shape;
    float half_width, half_height, radius; /* already scaled */
    float bat_line;                        /* already scaled */
    float camera_look_z, camera_distance;
    char skinmesh[64];
    bool randomize_texture;
} Board;

typedef struct Background {
    char skinmesh[64];
    bool randomize_texture;
} Background;

bool board_load(Board *board, const char *game_path);
bool background_load(Background *background, const char *game_path);

/* The image randomizetexture picks for this board, from a random number. */
void board_random_texture(const Board *board, int random, char *out, int out_size);
void background_random_texture(int random, char *out, int out_size);

#endif
