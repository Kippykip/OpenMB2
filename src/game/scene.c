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
static SceneBrick decode_brick(const LevelObject *object)
{
    SceneBrick b;
    const uint8_t *raw = object->raw;

    memset(&b, 0, sizeof(b));
    b.object = object;
    /* The original nudges every brick 0.01 to the left. */
    b.position = vec3((float)object->x - 0.01f, (float)object->y, (float)object->z);
    b.scale = 1.0f;
    b.flags = raw[8];
    b.rotation = raw[9];
    b.model = raw[12];
    b.height = raw[13];
    b.texture = raw[14];
    b.tile = raw[15];
    b.overlay_tile = raw[16];
    /* Glass always uses tile 0xF0 of texture 7, whatever the record says. */
    if (object->type == 0x18) {
        b.texture = 7;
        b.tile = 0xF0;
    }
    return b;
}

Mat4 scene_brick_matrix(const SceneBrick *b)
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

    Mat4 world = mat4_multiply(
        mat4_multiply(mat4_multiply(mat4_multiply(mat4_rotation_x(rx), mat4_rotation_y(ry)), mat4_rotation_z(rz)),
                      mat4_rotation_z(b->roll)),
        mat4_translation(b->position.x, b->position.y, b->position.z));

    if (b->scale != 1.0f) {
        Vec3 p = b->position;
        world = mat4_multiply(world, mat4_translation(-p.x, -p.y, -p.z));
        world = mat4_multiply(world, mat4_scaling(b->scale, b->scale, b->scale));
        world = mat4_multiply(world, mat4_translation(p.x, p.y, p.z));
    }

    /* The part of a tip done so far, about (x, y - 1, z) (0x402f36). */
    if (b->tip != 0.0f) {
        float turned = b->tip > 0.0f ? PI / 2.0f - b->tip : -PI / 2.0f - b->tip;
        Vec3 p = b->position;
        world = mat4_multiply(world, mat4_translation(-p.x, 1.0f - p.y, -p.z));
        world = mat4_multiply(world, mat4_rotation_z(turned));
        world = mat4_multiply(world, mat4_translation(p.x, p.y - 1.0f, p.z));
    }
    return world;
}

/* ------------------------------------------------------------------------ */
/* Brick model cache                                                        */
/* ------------------------------------------------------------------------ */

typedef struct CachedModel {
    char path[64];
    bool ok;
    XModel model;
} CachedModel;

struct ModelCache {
    CachedModel *items;
    int count;
};

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
    if (cache == NULL)
        return;
    for (i = 0; i < cache->count; i++) {
        if (cache->items[i].ok)
            xfile_free(&cache->items[i].model);
    }
    free(cache->items);
    free(cache);
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
                       int row_shift, bool mirror_u, bool mirror_v)
{
    const TextureScript *script = textureset_get(&scene->textures, texture);
    float xfactor = script ? script->xfactor : 1.0f;
    float yfactor = script ? script->yfactor : 1.0f;
    float column = (float)(tile >> 4), row = (float)((tile & 15) + row_shift);
    BrickBatch *batch = batch_for_texture(scene, texture);
    BrickSpan *span;
    Vec3 sum = vec3(0.0f, 0.0f, 0.0f);
    int i;

    if (batch == NULL || model->vertex_count == 0)
        return;
    if (batch->span_count == batch->span_capacity) {
        int capacity = batch->span_capacity ? batch->span_capacity * 2 : 64;
        BrickSpan *grown = realloc(batch->spans, (size_t)capacity * sizeof(*grown));
        if (grown == NULL)
            return;
        batch->spans = grown;
        batch->span_capacity = capacity;
    }
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

    span = &batch->spans[batch->span_count++];
    span->first = batch->vertex_count;
    span->count = model->vertex_count;
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
        sum = vec3_add(sum, p);
    }
    span->centre = vec3_scale(sum, 1.0f / (float)model->vertex_count);
}

static void bake_brick_at(Scene *scene, ModelCache *cache, const SceneBrick *b, Mat4 world)
{
    char path[64];
    const XModel *model;

    if (b->height <= 1)
        snprintf(path, sizeof(path), "Meshes/%d.x", b->model);
    else
        snprintf(path, sizeof(path), "Meshes/%dheight%d.x", b->model, b->height);
    model = cache_get(cache, path);
    if (model != NULL)
        bake_model(scene, model, world, b->texture, b->tile, b->cracks + b->flash, false, false);

    if (b->flags & 0x80) {
        snprintf(path, sizeof(path), "Meshes/%ddophgt%d.x", b->model, b->height < 1 ? 1 : b->height);
        model = cache_get(cache, path);
        if (model != NULL)
            bake_model(scene, model, world, b->texture + 1, b->overlay_tile, 0, (b->flags & 0x20) != 0,
                       (b->flags & 0x40) != 0);
    }
}

static void bake_brick(Scene *scene, ModelCache *cache, const SceneBrick *b)
{
    bake_brick_at(scene, cache, b, scene_brick_matrix(b));
}

/* A crumb: the brick's own model and rotations, spun, shrunk and moved. */
static void bake_crumb(Scene *scene, const SceneCrumb *c)
{
    SceneBrick b = scene->bricks[c->brick];
    Mat4 world;

    b.position = vec3(0.0f, 0.0f, 0.0f);
    b.scale = 1.0f;
    b.tip = 0.0f;
    world = scene_brick_matrix(&b);
    world = mat4_multiply(world, mat4_scaling(c->scale, c->scale, c->scale));
    world = mat4_multiply(world, mat4_rotation_axis(c->axis, c->angle));
    world = mat4_multiply(world, mat4_translation(c->position.x, c->position.y, c->position.z));
    bake_brick_at(scene, scene->models, &b, world);
}

/* ------------------------------------------------------------------------ */

/*
 * The board models put the top of their floor ("Doska") at y = -0.314,
 * but bricks stand on y = -1, and the original shows the board that much
 * lower. Find the floor top and return how far to move the board.
 */
static float board_floor_offset(const XModel *model)
{
    float top = -1e30f;
    int i, j;

    for (i = 0; i < model->part_count; i++) {
        const XPart *part = &model->parts[i];
        if (strcmp(part->frame, "Doska") != 0)
            continue;
        for (j = 0; j < part->vertex_count; j++) {
            float y = model->vertices[part->first_vertex + j].pos[1];
            if (y > top)
                top = y;
        }
    }
    return top > -1e29f ? -1.0f - top : 0.0f;
}

static bool load_skinmesh(XModel *model, const char *name)
{
    char path[128];
    if (name[0] == '\0')
        return false;
    snprintf(path, sizeof(path), "SkinMeshes/%s", name);
    return xfile_load(model, path);
}

static int compare_batches(const void *a, const void *b)
{
    return ((const BrickBatch *)a)->texture - ((const BrickBatch *)b)->texture;
}

void scene_bake_bricks(Scene *scene)
{
    int i;

    /* Keep each batch's memory, just empty it. */
    for (i = 0; i < scene->batch_count; i++)
        scene->batches[i].vertex_count = scene->batches[i].span_count = 0;
    for (i = 0; i < scene->brick_count; i++) {
        const SceneBrick *b = &scene->bricks[i];
        if (!b->hidden && b->animal == NULL && (!b->removed || b->dying_ms > 0))
            bake_brick(scene, scene->models, b);
    }
    for (i = 0; i < scene->crumb_count; i++)
        bake_crumb(scene, &scene->crumbs[i]);
    /* In texture order, which is the order the original draws them in. */
    qsort(scene->batches, (size_t)scene->batch_count, sizeof(*scene->batches), compare_batches);
}

const SceneAnimal *scene_animal(int type)
{
    static const SceneAnimal animals[] = {
        { 0xb8, "b_sheep.x", "Sheep1.ogg", "Sheep2.ogg", MODEL_ANIMAL },
        { 0xba, "b_sheep.x", "Sheep1.ogg", "Sheep2.ogg", MODEL_ANIMAL },
        { 0xbc, "b_elk.x", "Elk1.ogg", "Elk2.ogg", MODEL_ANIMAL },
        { 0xbe, "b_cow.x", "Cow1.ogg", "Cow2.ogg", MODEL_ANIMAL },
        { 0xc0, "b_ostrich.x", "Ostrich1.ogg", "Ostrich2.ogg", MODEL_ANIMAL },
        { 0xc2, "b_pinguin.x", "Pinguin1.ogg", "Pinguin2.ogg", MODEL_ANIMAL },
        { 0xc4, "b_boy.x", "Boy1.ogg", "Boy2.ogg", MODEL_ANIMAL },
        { 0xc6, "b_girl.x", "Girl1.ogg", "Girl2.ogg", MODEL_ANIMAL },
        { 0xc8, "b_camel.x", "Camel1.ogg", "Camel2.ogg", MODEL_ANIMAL },
        { 0xca, "b_jiraf.x", "Jiraf1.ogg", "Jiraf2.ogg", MODEL_ANIMAL },
        { 0xcc, "b_pig.x", "Pig1.ogg", "Pig2.ogg", MODEL_ANIMAL },
        { 0xce, "b_rabbit.x", "Rabbit1.ogg", "Rabbit2.ogg", MODEL_ANIMAL },
        { 0xd0, "b_bird.x", "Bird1.ogg", "Bird2.ogg", MODEL_ANIMAL },
        { 0xd2, "b_santa.x", "Santa1.ogg", "Santa2.ogg", MODEL_ANIMAL },
        { 0xd4, "b_alien.x", "Alien1.ogg", "Alien2.ogg", MODEL_ANIMAL },
        { 0xd6, "b_alien2.x", "Alien1.ogg", "Alien2.ogg", MODEL_ANIMAL },
        { 0xd8, "b_ufo.x", "StarShip1.ogg", "StarShip2.ogg", MODEL_ANIMAL },
        { 0xda, "b_butterfly.x", "Butterfly1.ogg", "Butterfly2.ogg", MODEL_ANIMAL },
        { 0xdc, "b_elephant.x", "Elephant1.ogg", "Elephant2.ogg", MODEL_ANIMAL },
        { 0xde, "b_crocodile.x", "Crocodile1.ogg", "Crocodile2.ogg", MODEL_ANIMAL },
        { 0xa4, "b_rocket.x", "Firework1.ogg", NULL, MODEL_ROCKET },
        { 0x84, "b_teleportsrc.x", NULL, NULL, MODEL_TELEPORT },
        { 0x88, "b_teleportdst.x", NULL, NULL, MODEL_TELEPORT_END },
        { 0xb0, "b_train.x", "Train3.ogg", NULL, MODEL_TRAIN },
        { 0xa0, "b_cannon.x", "br_cannon.ogg", NULL, MODEL_CANNON },
    };
    size_t i;

    for (i = 0; i < sizeof(animals) / sizeof(animals[0]); i++)
        if (animals[i].type == type)
            return &animals[i];
    return NULL;
}

bool scene_load(Scene *scene, const char *level_path, int random)
{
    const char *textureset_path, *background_path;
    int i;

    memset(scene, 0, sizeof(*scene));
    if (!level_load(&scene->level, level_path))
        return false;
    scene->models = calloc(1, sizeof(*scene->models));
    scene->bricks = calloc((size_t)(scene->level.object_count + 1), sizeof(*scene->bricks));
    scene->billboards = calloc((size_t)(scene->level.object_count + 1), sizeof(*scene->billboards));
    if (scene->models == NULL || scene->bricks == NULL || scene->billboards == NULL) {
        scene_free(scene);
        return false;
    }

    textureset_path = scene->level.slots[LEVEL_SLOT_TEXTURESET][0]
                          ? scene->level.slots[LEVEL_SLOT_TEXTURESET]
                          : "textureset.txt";
    if (!textureset_load(&scene->textures, textureset_path))
        log_warn("%s: can't load texture set %s", level_path, textureset_path);

    if (board_load(&scene->board, scene->level.slots[LEVEL_SLOT_BOARD])) {
        if (load_skinmesh(&scene->board_model, scene->board.skinmesh))
            scene->board_y = board_floor_offset(&scene->board_model);
        scene_randomize_board(scene, random);
    } else {
        log_warn("%s: can't load board %s", level_path, scene->level.slots[LEVEL_SLOT_BOARD]);
    }

    /*
     * Some levels (e.g. 030_Volkano) leave the background slot empty; the
     * original still shows the usual background box then.
     */
    background_path = scene->level.slots[LEVEL_SLOT_BACKGROUND][0]
                          ? scene->level.slots[LEVEL_SLOT_BACKGROUND]
                          : "Backgrounds/bgnone.txt";
    if (background_load(&scene->background, background_path)) {
        load_skinmesh(&scene->background_model, scene->background.skinmesh);
        scene_randomize_background(scene, random / 10);
    }

    for (i = 0; i < scene->level.object_count; i++) {
        const LevelObject *object = &scene->level.objects[i];
        if (level_object_is_brick(object)) {
            SceneBrick *b = &scene->bricks[scene->brick_count++];
            *b = decode_brick(object);
            /* Glass (type 0x18) is invisible until the ball first hits it. */
            b->hidden = object->type == 0x18;
        } else if (scene_animal(object->type) != NULL) {
            SceneBrick *b = &scene->bricks[scene->brick_count++];
            *b = decode_brick(object);
            b->animal = scene_animal(object->type);
            b->rotation &= 0x3c; /* turned about Y only (0x420b70) */
            b->animation_ms = (uint32_t)(rand() % 5000);
        } else if (object->type == 0x64 || object->type == 0x90 || object->type == 0x68 ||
                   object->type == 0x94) {
            SceneBillboard *bb = &scene->billboards[scene->billboard_count++];
            bb->object = object;
            bb->position = vec3((float)object->x - 0.01f, (float)object->y, (float)object->z);
            bb->water = object->type == 0x68 || object->type == 0x94;
        } else {
            scene->other_count++;
        }
    }
    scene_bake_bricks(scene);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Breaking                                                                 */
/* ------------------------------------------------------------------------ */

#define DYING_MS        2000 /* 0x403d20 */
#define CRUMBS          5    /* 0x425bd0 */
#define CRUMB_LIFE_MS   2200 /* the group's life (0x425cd0); a crumb's own is 3000 */
#define CRUMB_SCALE     0.3f /* guess, from a video of the original */
#define CRUMB_FLOOR     -0.75f
#define EXPLOSION_FRAME_MS 75

/* rand() % 100 * scale, as the original picks its random numbers. */
static float random_percent(float scale)
{
    return (float)(rand() % 100) * scale;
}

void scene_add_explosion(Scene *scene, Vec3 position)
{
    if (scene->explosion_count < SCENE_MAX_EXPLOSIONS) {
        SceneExplosion *e = &scene->explosions[scene->explosion_count++];
        e->position = position;
        e->age = 0;
    }
}

void scene_break_brick(Scene *scene, int index)
{
    SceneBrick *b = &scene->bricks[index];
    int i;

    b->dying_ms = DYING_MS;
    if (b->hidden)
        return;

    scene_add_explosion(scene, b->position);
    /* 0x425870 */
    for (i = 0; i < CRUMBS && scene->crumb_count < SCENE_MAX_CRUMBS; i++) {
        SceneCrumb *c = &scene->crumbs[scene->crumb_count++];
        memset(c, 0, sizeof(*c));
        c->brick = index;
        c->position = b->position;
        c->velocity = vec3((random_percent(0.02f) - 1.0f) * 0.35f, (random_percent(0.01f) + 0.5f) * 0.35f,
                           (random_percent(0.02f) - 1.0f) * 0.35f);
        c->bounce = c->velocity;
        c->axis = vec3(random_percent(0.02f) - 1.0f, random_percent(0.02f) - 1.0f,
                       random_percent(0.02f) - 1.0f);
        if (vec3_length(c->axis) < 0.01f)
            c->axis = vec3(0.0f, 1.0f, 0.0f);
        c->axis = vec3_normalize(c->axis);
        c->spin = random_percent(0.00025f);
        c->scale = CRUMB_SCALE;
    }
}

bool scene_update_effects(Scene *scene, uint32_t ms)
{
    float dt = (float)ms * 0.01f;
    bool moved = false;
    int i, kept;

    /* A dying brick shrinks by half of dt a step (0x402e40). */
    for (i = 0; i < scene->brick_count; i++) {
        SceneBrick *b = &scene->bricks[i];
        if (b->dying_ms == 0)
            continue;
        b->dying_ms = b->dying_ms > ms ? b->dying_ms - ms : 0;
        b->scale *= 1.0f - 0.5f * dt;
        if (b->scale < 0.0f)
            b->scale = 0.0f;
        moved = true;
    }

    /* Crumbs fly, fall, bounce and shrink (0x425a00). */
    for (i = 0, kept = 0; i < scene->crumb_count; i++) {
        SceneCrumb *c = &scene->crumbs[i];
        c->age += ms;
        if (c->age > CRUMB_LIFE_MS)
            continue;
        c->position = vec3_add(c->position, vec3_scale(c->velocity, dt));
        if (c->position.y <= CRUMB_FLOOR) {
            c->position.y = -0.74f;
            if (c->bounce.y > 0.05f) {
                c->bounce = vec3_scale(c->bounce, 0.75f);
                c->velocity = c->bounce;
            } else {
                c->velocity.y = 0.0f;
            }
        } else {
            c->velocity.y -= dt * 0.1f;
        }
        c->angle += c->spin;
        c->scale *= 0.98f;
        scene->crumbs[kept++] = *c;
        moved = true;
    }
    scene->crumb_count = kept;

    /* 16 frames of 75 ms (0x406030). */
    for (i = 0, kept = 0; i < scene->explosion_count; i++) {
        SceneExplosion *e = &scene->explosions[i];
        e->age += ms;
        if (e->age / EXPLOSION_FRAME_MS > 15)
            continue;
        scene->explosions[kept++] = *e;
    }
    scene->explosion_count = kept;
    return moved;
}

void scene_randomize_board(Scene *scene, int random)
{
    board_random_texture(&scene->board, random, scene->board_texture,
                         sizeof(scene->board_texture));
}

void scene_randomize_background(Scene *scene, int random)
{
    background_random_texture(random, scene->background_texture,
                              sizeof(scene->background_texture));
}

void scene_free(Scene *scene)
{
    int i;

    for (i = 0; i < scene->batch_count; i++) {
        free(scene->batches[i].vertices);
        free(scene->batches[i].spans);
    }
    free(scene->batches);
    free(scene->bricks);
    free(scene->billboards);
    cache_free(scene->models);
    xfile_free(&scene->board_model);
    xfile_free(&scene->background_model);
    textureset_free(&scene->textures);
    level_free(&scene->level);
    memset(scene, 0, sizeof(*scene));
}
