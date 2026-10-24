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
    RENDER_NOT_LIGHTED = 1 << 6,
    RENDER_NO_DEPTH_TEST = 1 << 16, /* drawn over everything */
    RENDER_TINT = 1 << 17           /* not lit: the texture times the diffuse colour */
};

typedef struct RenderPart {
    int first_vertex, vertex_count;
    unsigned texture; /* from render_texture() */
    float diffuse[4];
    uint32_t flags;
    float uv_speed[2]; /* texture scrolling, UV units per millisecond */
    float uv_pulse[2]; /* texture stretching back and forth, scale units per millisecond */
    int pose;          /* index into render_draw_posed()'s matrices, -1 for none */
    /* How long its normals are once transformed; Direct3D leaves them
       unnormalised, so a mesh scaled up in its file is lit more dimly.
       0 means 1. */
    float normal_length;
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

typedef enum RenderPass {
    RENDER_PASS_SOLID,   /* only the parts that aren't blended */
    RENDER_PASS_BLENDED, /* only the blended ones */
    RENDER_PASS_ALL      /* every part, in order */
} RenderPass;

/*
 * Draw a mesh. Usually every mesh is drawn with RENDER_PASS_SOLID, then
 * again with RENDER_PASS_BLENDED, so blended parts go over everything
 * solid.
 */
void render_draw(const RenderMesh *mesh, Mat4 model, RenderPass pass, float time_ms);
/* The same, moving each part with a pose >= 0 by pose[part.pose] first. */
void render_draw_posed(const RenderMesh *mesh, Mat4 model, const Mat4 *pose, RenderPass pass,
                       float time_ms);

/*
 * Fade everything drawn from now on to this opacity (1 = normal). Faded
 * parts are blended, so draw them in a blended or RENDER_PASS_ALL pass.
 */
void render_set_fade(float alpha);

#endif
