/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * xfile.h - loader for DirectX .x model files.
 *
 * All of the game's models are .x files: bricks and their height variants
 * in Meshes/ (mostly text), and the boards, bats, prizes and animals in
 * SkinMeshes/ (mostly binary). Both encodings share one reader.
 *
 * A model is flattened into a plain triangle list: every frame transform
 * is applied, so vertices come out in model space, and the triangles are
 * grouped into parts that share a material. That is all the game needs
 * for drawing; skin weights and animation sets are skipped for now.
 */
#ifndef OPENMB2_XFILE_H
#define OPENMB2_XFILE_H

#include <stdbool.h>
#include <stddef.h>

/* Same layout as the game's own vertices: 32 bytes. */
typedef struct XVertex {
    float pos[3];
    float normal[3];
    float uv[2];
} XVertex;

typedef struct XMaterial {
    float diffuse[4]; /* r, g, b, a */
    float power;
    float specular[3];
    float emissive[3];
    char texture[128]; /* "" when the material has no texture */
} XMaterial;

/* A run of triangles (3 vertices each) drawn with one material. */
typedef struct XPart {
    char frame[32]; /* name of the frame holding the mesh, e.g. "Doska" */
    int material;
    int first_vertex;
    int vertex_count;
} XPart;

typedef struct XModel {
    XVertex *vertices;
    int vertex_count;
    XMaterial *materials;
    int material_count;
    XPart *parts;
    int part_count;
    float min[3], max[3]; /* bounding box */
} XModel;

/* Parse a whole .x file held in memory. Names the file in error messages. */
bool xfile_parse(XModel *model, const void *data, size_t size, const char *name);
/* Read and parse a file from the game folder. */
bool xfile_load(XModel *model, const char *game_path);
void xfile_free(XModel *model);

#endif
