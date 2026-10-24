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
 * grouped into parts that share a material. The frame tree and its
 * animation keys are kept too, so a part can be moved by its animated
 * frame (the bat's fans and turbines). Skin weights are skipped.
 */
#ifndef OPENMB2_XFILE_H
#define OPENMB2_XFILE_H

#include "matrix.h"

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
    char path[160]; /* every named frame it is in, outermost first: "/Corpus/L_Fan/" */
    int frame_index; /* innermost frame in XModel.frames, -1 for none */
    int material;
    int first_vertex;
    int vertex_count;
} XPart;

/* A key: a time in ticks, then 4 (quaternion w x y z), 3 or 16 values. */
typedef struct XKey {
    unsigned time;
    float value[16];
} XKey;

typedef struct XKeys {
    XKey *keys;
    int count;
} XKeys;

typedef struct XFrame {
    char name[32];     /* "" for an unnamed frame */
    int parent;        /* index in XModel.frames, -1 at the top */
    Mat4 local;        /* FrameTransformMatrix */
    Mat4 bind;         /* local * all its parents: where the vertices were put */
    /* Animation keys, if an AnimationSet names this frame. */
    XKeys rotation, scale, position, matrix;
} XFrame;

typedef struct XModel {
    XVertex *vertices;
    int vertex_count;
    XMaterial *materials;
    int material_count;
    XPart *parts;
    int part_count;
    XFrame *frames; /* parents come before their children */
    int frame_count;
    float min[3], max[3]; /* bounding box */
} XModel;

/* Parse a whole .x file held in memory. Names the file in error messages. */
bool xfile_parse(XModel *model, const void *data, size_t size, const char *name);
/* Read and parse a file from the game folder. */
bool xfile_load(XModel *model, const char *game_path);
void xfile_free(XModel *model);

#define XFRAME_REST 0xffffffffu

int xmodel_find_frame(const XModel *model, const char *name);

/*
 * Pose the frames: times[f] is the animation time of frame f in ticks
 * (the game uses milliseconds), looping at the last key, as the game's
 * frame code does (0x421090); XFRAME_REST keeps the frame where the file
 * put it, as the game does until it first sets a frame's time. parts[p] gets the matrix that moves part p
 * from where it was loaded to its posed place.
 */
void xmodel_pose(const XModel *model, const unsigned *times, Mat4 *parts);

/* Is the part in this frame, or in a frame inside it? */
bool xpart_in_frame(const XPart *part, const char *frame);

#endif
