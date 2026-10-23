/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "board.h"
#include "../core/config.h"

#include <stdio.h>
#include <string.h>

bool board_load(Board *board, const char *game_path)
{
    Config cfg;
    float camera[2] = { 8.0f, 37.0f };
    float scale_x, scale_z;

    memset(board, 0, sizeof(*board));
    board->scale_x = board->scale_z = 1.0f;
    board->camera_look_z = camera[0];
    board->camera_distance = camera[1];
    if (!config_load(&cfg, game_path))
        return false;

    if (config_has(&cfg, "round"))
        board->shape = BOARD_ROUND;
    else if (config_has(&cfg, "compaund"))
        board->shape = BOARD_COMPOUND;
    else
        board->shape = BOARD_RECT;

    scale_x = config_get_float(&cfg, "scalex", 1.0f);
    scale_z = config_get_float(&cfg, "scalez", 1.0f);
    board->scale_x = scale_x;
    board->scale_z = scale_z;
    /* The original's defaults; round boards rely on the radius one. */
    board->half_width = config_get_float(&cfg, "halfwidth", 10.0f) * scale_x;
    board->radius = config_get_float(&cfg, "radius", 16.0f) * scale_x;
    board->half_height = config_get_float(&cfg, "halfheight", 10.0f) * scale_z;
    board->bat_line = config_get_float(&cfg, "batline", 0.0f) * scale_z;

    if (config_get_floats(&cfg, "camera", camera, 2) == 2) {
        board->camera_look_z = camera[0];
        board->camera_distance = camera[1];
    }
    snprintf(board->skinmesh, sizeof(board->skinmesh), "%s",
             config_get_string(&cfg, "skinmesh", ""));

    config_free(&cfg);
    return true;
}

bool background_load(Background *background, const char *game_path)
{
    Config cfg;

    memset(background, 0, sizeof(*background));
    if (!config_load(&cfg, game_path))
        return false;
    snprintf(background->skinmesh, sizeof(background->skinmesh), "%s",
             config_get_string(&cfg, "skinmesh", ""));
    config_free(&cfg);
    return true;
}

void board_random_texture(const Board *board, int random, char *out, int out_size)
{
    const char *format = board->shape == BOARD_ROUND      ? "Board/Round%2.2d.jpg"
                         : board->shape == BOARD_COMPOUND ? "Board/Semicircular%2.2d.jpg"
                                                          : "Board/Square%2.2d.jpg";
    snprintf(out, (size_t)out_size, format, random % 10);
}

void background_random_texture(int random, char *out, int out_size)
{
    snprintf(out, (size_t)out_size, "BG/bg%2.2d.jpg", random % 28);
}
