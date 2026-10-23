/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * render.h - a small 3D renderer on OpenGL ES 2.
 *
 * It reproduces what the game asked of Direct3D 8's fixed-function
 * pipeline: one directional light with ambient, textures modulated by the
 * lit material colour, optional alpha blending, and per-texture switches
 * for culling, depth writes and lighting. Everything is drawn from
 * meshes of XVertex (the game's 32-byte vertex) split into parts.
 */
#ifndef OPENMB2_RENDER_H
#define OPENMB2_RENDER_H

#include "../core/matrix.h"
#include "../core/xfile.h"

#include <stdbool.h>
#include <stdint.h>

/* Per-part switches; the TEXFLAG_* bits from textures.h use the same values. */
enum {
    RENDER_ALPHA = 1 << 0,
    RENDER_Z_DISABLE = 1 << 4,
    RENDER_TWO_SIDED = 1 << 5,
    RENDER_NOT_LIGHTED = 1 << 6
};

typedef struct RenderPart {
    int first_vertex, vertex_count;
    unsigned texture; /* from render_texture() */
    float diffuse[4];
    uint32_t flags;
    float uv_speed[2]; /* texture scrolling, UV units per millisecond */
} RenderPart;

typedef struct RenderMesh {
    unsigned buffer;
    int vertex_count;
    RenderPart *parts;
    int part_count;
} RenderMesh;

typedef struct RenderLight {
    float direction[3];
    float ambient[4], diffuse[4];
} RenderLight;

bool render_init(void);
void render_shutdown(void);

/*
 * The GL texture for an image in the game folder, loaded on first use and
 * kept until render_shutdown(). A missing or broken image gives a plain
 * white texture, so a model still shows up.
 */
unsigned render_texture(const char *game_path);

bool render_mesh_create(RenderMesh *mesh, const XVertex *vertices, int vertex_count);
void render_mesh_add_part(RenderMesh *mesh, const RenderPart *part);
void render_mesh_destroy(RenderMesh *mesh);

/* Start a frame: viewport, clear, camera and light. */
void render_begin(int width, int height, Mat4 view, Mat4 projection, const RenderLight *light);

/*
 * Draw a mesh. Call once with alpha_pass false for every mesh, then again
 * with true, so blended parts are drawn over everything solid.
 */
void render_draw(const RenderMesh *mesh, Mat4 model, bool alpha_pass, float time_ms);

#endif
