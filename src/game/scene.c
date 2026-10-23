/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "scene.h"
#include "../core/log.h"
#include "../core/matrix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f

/* ------------------------------------------------------------------------ */
/* Brick records                                                            */
/* ------------------------------------------------------------------------ */

/*
 * The parts of a brick record the original's brick constructor reads
 * (see docs/formats.md for the full record):
 *
 *   byte 8   flags: 0x80 has an overlay ("dop") model, 0x20 / 0x40 mirror
 *            the overlay's U / V
 *   byte 9   rotation bits, applied about X, then Y, then Z:
 *              0x01 X +90   0x02 X +180
 *              0x04 Y +22.5 0x08 Y +45  0x10 Y +90  0x20 Y +180
 *              0x40 Z +90   0x80 Z +180
 *   byte 12  model number: Meshes/<n>.x
 *   byte 13  height in layers: 1 = Meshes/<n>.x, h = Meshes/<n>height<h>.x
 *   byte 14  texture script index (see textures.h)
 *   byte 15  tile in the texture sheet: column in the high nibble, row low
 *   byte 16  overlay tile; the overlay uses texture script byte 14 + 1
 */
typedef struct Brick {
    Vec3 position;
    uint8_t flags, rotation;
    int model, height, texture, tile, overlay_tile;
} Brick;

static Brick decode_brick(const LevelObject *object)
{
    Brick b;
    const uint8_t *raw = object->raw;

    /* The original nudges every brick 0.01 to the left. */
    b.position = vec3((float)object->x - 0.01f, (float)object->y, (float)object->z);
    b.flags = raw[8];
    b.rotation = raw[9];
    b.model = raw[12];
    b.height = raw[13];
    b.texture = raw[14];
    b.tile = raw[15];
    b.overlay_tile = raw[16];
    return b;
}

static Mat4 brick_matrix(const Brick *b)
{
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;

    if (b->rotation & 0x01) rx += PI / 2.0f;
    if (b->rotation & 0x02) rx += PI;
    if (b->rotation & 0x04) ry += PI / 8.0f;
    if (b->rotation & 0x08) ry += PI / 4.0f;
    if (b->rotation & 0x10) ry += PI / 2.0f;
    if (b->rotation & 0x20) ry += PI;
    if (b->rotation & 0x40) rz += PI / 2.0f;
    if (b->rotation & 0x80) rz += PI;

    return mat4_multiply(
        mat4_multiply(mat4_multiply(mat4_rotation_x(rx), mat4_rotation_y(ry)), mat4_rotation_z(rz)),
        mat4_translation(b->position.x, b->position.y, b->position.z));
}

/* ------------------------------------------------------------------------ */
/* Brick model cache                                                        */
/* ------------------------------------------------------------------------ */

typedef struct CachedModel {
    char path[64];
    bool ok;
    XModel model;
} CachedModel;

typedef struct ModelCache {
    CachedModel *items;
    int count;
} ModelCache;

static const XModel *cache_get(ModelCache *cache, const char *path)
{
    CachedModel *grown;
    int i;

    for (i = 0; i < cache->count; i++) {
        if (strcmp(cache->items[i].path, path) == 0)
            return cache->items[i].ok ? &cache->items[i].model : NULL;
    }
    grown = realloc(cache->items, (size_t)(cache->count + 1) * sizeof(*grown));
    if (grown == NULL)
        return NULL;
    cache->items = grown;
    snprintf(grown[cache->count].path, sizeof(grown->path), "%s", path);
    grown[cache->count].ok = xfile_load(&grown[cache->count].model, path);
    cache->count++;
    return grown[cache->count - 1].ok ? &grown[cache->count - 1].model : NULL;
}

static void cache_free(ModelCache *cache)
{
    int i;
    for (i = 0; i < cache->count; i++) {
        if (cache->items[i].ok)
            xfile_free(&cache->items[i].model);
    }
    free(cache->items);
}

/* ------------------------------------------------------------------------ */
/* Baking bricks                                                            */
/* ------------------------------------------------------------------------ */

static BrickBatch *batch_for_texture(Scene *scene, int texture)
{
    BrickBatch *grown;
    int i;

    for (i = 0; i < scene->batch_count; i++) {
        if (scene->batches[i].texture == texture)
            return &scene->batches[i];
    }
    grown = realloc(scene->batches, (size_t)(scene->batch_count + 1) * sizeof(*grown));
    if (grown == NULL)
        return NULL;
    scene->batches = grown;
    memset(&grown[scene->batch_count], 0, sizeof(*grown));
    grown[scene->batch_count].texture = texture;
    return &grown[scene->batch_count++];
}

/*
 * Append a model to a batch: vertices go to world space, and UVs are
 * mapped into one tile of the texture sheet:
 *     u' = (u + column) * xfactor,  v' = (v + row) * yfactor
 */
static void bake_model(Scene *scene, const XModel *model, Mat4 world, int texture, int tile,
                       bool mirror_u, bool mirror_v)
{
    const TextureScript *script = textureset_get(&scene->textures, texture);
    float xfactor = script ? script->xfactor : 1.0f;
    float yfactor = script ? script->yfactor : 1.0f;
    float column = (float)(tile >> 4), row = (float)(tile & 15);
    BrickBatch *batch = batch_for_texture(scene, texture);
    int i;

    if (batch == NULL)
        return;
    if (batch->vertex_count + model->vertex_count > batch->capacity) {
        int capacity = batch->capacity ? batch->capacity : 1024;
        XVertex *grown;
        while (capacity < batch->vertex_count + model->vertex_count)
            capacity *= 2;
        grown = realloc(batch->vertices, (size_t)capacity * sizeof(*grown));
        if (grown == NULL)
            return;
        batch->vertices = grown;
        batch->capacity = capacity;
    }

    for (i = 0; i < model->vertex_count; i++) {
        const XVertex *in = &model->vertices[i];
        XVertex *out = &batch->vertices[batch->vertex_count++];
        Vec3 p = mat4_transform_point(world, vec3(in->pos[0], in->pos[1], in->pos[2]));
        Vec3 n = vec3_normalize(
            mat4_transform_normal(world, vec3(in->normal[0], in->normal[1], in->normal[2])));
        float u = mirror_u ? 1.0f - in->uv[0] : in->uv[0];
        float v = mirror_v ? 1.0f - in->uv[1] : in->uv[1];

        out->pos[0] = p.x;
        out->pos[1] = p.y;
        out->pos[2] = p.z;
        out->normal[0] = n.x;
        out->normal[1] = n.y;
        out->normal[2] = n.z;
        out->uv[0] = (u + column) * xfactor;
        out->uv[1] = (v + row) * yfactor;
    }
}

static void bake_brick(Scene *scene, ModelCache *cache, const Brick *b)
{
    char path[64];
    const XModel *model;
    Mat4 world = brick_matrix(b);

    if (b->height <= 1)
        snprintf(path, sizeof(path), "Meshes/%d.x", b->model);
    else
        snprintf(path, sizeof(path), "Meshes/%dheight%d.x", b->model, b->height);
    model = cache_get(cache, path);
    if (model != NULL)
        bake_model(scene, model, world, b->texture, b->tile, false, false);

    if (b->flags & 0x80) {
        snprintf(path, sizeof(path), "Meshes/%ddophgt%d.x", b->model, b->height < 1 ? 1 : b->height);
        model = cache_get(cache, path);
        if (model != NULL)
            bake_model(scene, model, world, b->texture + 1, b->overlay_tile, (b->flags & 0x20) != 0,
                       (b->flags & 0x40) != 0);
    }
}

/* ------------------------------------------------------------------------ */

static bool load_skinmesh(XModel *model, const char *name)
{
    char path[128];
    if (name[0] == '\0')
        return false;
    snprintf(path, sizeof(path), "SkinMeshes/%s", name);
    return xfile_load(model, path);
}

bool scene_load(Scene *scene, const char *level_path, int random)
{
    ModelCache cache = { NULL, 0 };
    const char *textureset_path;
    int i;

    memset(scene, 0, sizeof(*scene));
    if (!level_load(&scene->level, level_path))
        return false;

    textureset_path = scene->level.slots[LEVEL_SLOT_TEXTURESET][0]
                          ? scene->level.slots[LEVEL_SLOT_TEXTURESET]
                          : "textureset.txt";
    if (!textureset_load(&scene->textures, textureset_path))
        log_warn("%s: can't load texture set %s", level_path, textureset_path);

    if (board_load(&scene->board, scene->level.slots[LEVEL_SLOT_BOARD])) {
        load_skinmesh(&scene->board_model, scene->board.skinmesh);
        if (scene->board.randomize_texture)
            board_random_texture(&scene->board, random, scene->board_texture,
                                 sizeof(scene->board_texture));
    } else {
        log_warn("%s: can't load board %s", level_path, scene->level.slots[LEVEL_SLOT_BOARD]);
    }

    if (background_load(&scene->background, scene->level.slots[LEVEL_SLOT_BACKGROUND])) {
        load_skinmesh(&scene->background_model, scene->background.skinmesh);
        if (scene->background.randomize_texture)
            background_random_texture(random / 10, scene->background_texture,
                                      sizeof(scene->background_texture));
    }

    for (i = 0; i < scene->level.object_count; i++) {
        const LevelObject *object = &scene->level.objects[i];
        if (level_object_is_brick(object)) {
            Brick brick = decode_brick(object);
            bake_brick(scene, &cache, &brick);
            scene->brick_count++;
        } else {
            scene->other_count++;
        }
    }

    cache_free(&cache);
    return true;
}

void scene_free(Scene *scene)
{
    int i;

    for (i = 0; i < scene->batch_count; i++)
        free(scene->batches[i].vertices);
    free(scene->batches);
    xfile_free(&scene->board_model);
    xfile_free(&scene->background_model);
    textureset_free(&scene->textures);
    level_free(&scene->level);
    memset(scene, 0, sizeof(*scene));
}
