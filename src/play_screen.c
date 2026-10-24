/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "play_screen.h"
#include "audio/audio.h"
#include "core/config.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/vfs.h"
#include "game/play.h"
#include "game/scene.h"
#include "render/render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LEVELS 256

/* The original looks for level packs in these folders. */
static const char *const k_pack_folders[] = {
    "Levels", "Levels1", "Levels2", "Levels3", "Levels4",
    "Levels5", "Levels6", "Levels7", "Levels0r"
};
#define PACK_FOLDER_COUNT ((int)(sizeof(k_pack_folders) / sizeof(k_pack_folders[0])))

typedef struct Pack {
    const char *folder;
    char (*levels)[128];
    int level_count;
} Pack;

/*
 * The game's camera, from its camera object: it orbits the point
 * (0, 0, look_z) at distance * zoom, turned by yaw / pitch / roll.
 * A level starts at pitch 50 degrees, yaw 0, roll 0.05.
 */
typedef struct Camera {
    float look_z, distance, zoom;
    float yaw, pitch, roll;
} Camera;

#define START_LIVES 3
#define LEVEL_DONE_MS 2000 /* guess: how long before the next level */

static struct {
    SDL_Window *window;
    PlayScreenOptions options;
    Pack packs[PACK_FOLDER_COUNT];
    int pack_count;
    int pack, level; /* what's showing */
    Scene scene;
    bool scene_loaded;
    Play play;
    bool playing;
    int score, lives;  /* carried from level to level */
    int music;         /* which level tune is playing, 0-3 */
    RenderMesh shadows, glows, prize_glows, trail, bullet_shadows, strings, billboards;
    RenderMesh bolts;
    unsigned bolt_texture;
    int train_sounds[64];   /* each train brick's engine loop, by brick */
    uint32_t object_time;
    unsigned shadow_texture, glow_texture, particle_texture, rubber_texture, billboard_texture;
    XModel bullet_models[SHOT_KINDS];
    RenderMesh bullet_meshes[SHOT_KINDS];
    XModel firework_model;
    RenderMesh firework_mesh;
    XModel prize_models[PRIZE_KINDS];
    RenderMesh prize_meshes[PRIZE_KINDS];
    bool prize_loaded[PRIZE_KINDS]; /* tried to load: models stay for every level */
    /* Animal models, loaded the first time a level has one; they stay too. */
    struct {
        const SceneAnimal *animal;
        XModel model;
        RenderMesh mesh;
    } animals[32];
    int animal_count;
    int test_prize;                 /* the next kind F9 drops */
    unsigned turbine_time; /* the turbines' animation time, XFRAME_REST at first */
    XModel bat_model, ball_model, explosive_model, hypnotic_model;
    RenderMesh background, board, bricks, bat, ball, explosive_ball, hypnotic_ball;
    RenderMesh explosions;      /* rebuilt every frame */
    unsigned explosion_texture;
    RenderLight light;
    Camera camera;
    bool dragging;
    uint32_t time;     /* ms, for scrolling textures */
    char title[320];
} v;

/* ------------------------------------------------------------------------ */
/* Level packs                                                              */
/* ------------------------------------------------------------------------ */

static void add_level(const char *name, bool is_dir, void *user)
{
    Pack *pack = user;
    size_t length = strlen(name);

    if (is_dir || length < 4 || SDL_strcasecmp(name + length - 4, ".mb2") != 0)
        return;
    if (pack->level_count >= MAX_LEVELS)
        return;
    snprintf(pack->levels[pack->level_count++], 128, "%s", name);
}

static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void find_packs(void)
{
    int i;

    for (i = 0; i < PACK_FOLDER_COUNT; i++) {
        Pack *pack = &v.packs[v.pack_count];

        if (!vfs_is_dir(k_pack_folders[i]))
            continue;
        pack->folder = k_pack_folders[i];
        pack->levels = calloc(MAX_LEVELS, sizeof(*pack->levels));
        pack->level_count = 0;
        if (pack->levels == NULL)
            continue;
        vfs_list(pack->folder, add_level, pack);
        if (pack->level_count == 0) {
            free(pack->levels);
            continue;
        }
        qsort(pack->levels, (size_t)pack->level_count, sizeof(*pack->levels), compare_names);
        v.pack_count++;
    }
}

/* What blend_positions() moved, to put back after drawing. */
typedef struct UnblendedBrick {
    Vec3 position;
    float tip, scale, heading;
} UnblendedBrick;

static struct {
    Vec3 balls[PLAY_MAX_BALLS], prizes[PLAY_MAX_PRIZES], bullets[PLAY_MAX_BULLETS];
    Vec3 pieces[BAT_PIECES];
    float spins[PLAY_MAX_BALLS];
    Vec3 particles[PLAY_MAX_PARTICLES];
    Vec3 smoke[PLAY_MAX_SMOKE];
    Vec3 sparks[PLAY_MAX_SPARKS];
    Vec3 fireworks[PLAY_MAX_FIREWORKS];
    UnblendedBrick *bricks;     /* one per scene brick */
    int brick_capacity;
    bool bricks_saved;          /* bricks[] holds this frame's bricks */
    bool bricks_blended;        /* the baked bricks are blended ones */
} unblended;

/* ------------------------------------------------------------------------ */
/* Turning a scene into GPU meshes                                          */
/* ------------------------------------------------------------------------ */

static unsigned texture_for_name(const char *name)
{
    char path[160];
    if (!texture_find_image(name, path, sizeof(path))) {
        if (name[0] != '\0')
            log_warn("texture '%s' not found", name);
        return render_texture(NULL);
    }
    return render_texture(path);
}

/*
 * A model from SkinMeshes: one part per material. override replaces the
 * texture of every part in override_frame ("" = all parts). keep, if not
 * NULL, picks the parts to draw.
 */
static void build_model_mesh(RenderMesh *mesh, const XModel *model, uint32_t flags,
                             const char *override, const char *override_frame,
                             bool (*keep)(const XPart *part))
{
    int i;

    if (!render_mesh_create(mesh, model->vertices, model->vertex_count))
        return;
    for (i = 0; i < model->part_count; i++) {
        const XPart *xpart = &model->parts[i];
        const XMaterial *material = xpart->material >= 0 ? &model->materials[xpart->material] : NULL;
        RenderPart part;
        bool overridden = override[0] != '\0' &&
                          (override_frame[0] == '\0' || strcmp(xpart->frame, override_frame) == 0);

        if (keep != NULL && !keep(xpart))
            continue;
        memset(&part, 0, sizeof(part));
        part.first_vertex = xpart->first_vertex;
        part.vertex_count = xpart->vertex_count;
        part.flags = flags;
        part.pose = i;
        if (material != NULL) {
            memcpy(part.diffuse, material->diffuse, sizeof(part.diffuse));
            part.texture = texture_for_name(overridden ? override : material->texture);
        } else {
            part.diffuse[0] = part.diffuse[1] = part.diffuse[2] = part.diffuse[3] = 1.0f;
            part.texture = texture_for_name(overridden ? override : "");
        }
        render_mesh_add_part(mesh, &part);
    }
}

/* All brick batches go in one buffer, one part per texture script. */
static void build_brick_mesh(void)
{
    const Scene *s = &v.scene;
    XVertex *all;
    int total = 0, at = 0, i;

    render_mesh_destroy(&v.bricks);
    for (i = 0; i < s->batch_count; i++)
        total += s->batches[i].vertex_count;
    if (total == 0)
        return;
    all = malloc((size_t)total * sizeof(*all));
    if (all == NULL)
        return;
    for (i = 0; i < s->batch_count; i++) {
        memcpy(all + at, s->batches[i].vertices,
               (size_t)s->batches[i].vertex_count * sizeof(*all));
        at += s->batches[i].vertex_count;
    }
    if (!render_mesh_create(&v.bricks, all, total)) {
        free(all);
        return;
    }
    free(all);

    at = 0;
    for (i = 0; i < s->batch_count; i++) {
        const TextureScript *script = textureset_get(&s->textures, s->batches[i].texture);
        RenderPart part;

        memset(&part, 0, sizeof(part));
        part.first_vertex = at;
        part.vertex_count = s->batches[i].vertex_count;
        part.diffuse[0] = part.diffuse[1] = part.diffuse[2] = part.diffuse[3] = 1.0f;
        /*
         * Bricks are drawn without back-face culling: single-sided parts
         * such as grass blades show from both sides in the original.
         */
        part.flags = RENDER_TWO_SIDED;
        if (script != NULL) {
            part.flags |= script->flags & (RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_NOT_LIGHTED);
            /* Glass shows only its outside faces in the original. */
            if (script->flags & TEXFLAG_ALPHA)
                part.flags &= ~(uint32_t)RENDER_TWO_SIDED;
            part.texture = (script->flags & TEXFLAG_FAKE) ? render_texture(NULL)
                                                           : texture_for_name(script->image);
            /* movex/movey and scalex/scaley are steps per frame drawn (0x410110),
               taken at the original's ~60 fps. Water's scaley takes it from
               0.75 to 1.25 in 200 frames, so its ripples turn round every 3.3 s. */
            part.uv_speed[0] = script->movex / 16.67f;
            part.uv_speed[1] = script->movey / 16.67f;
            part.uv_pulse[0] = script->scalex / 16.67f;
            part.uv_pulse[1] = script->scaley / 16.67f;
        } else {
            log_warn("brick texture index %d is not in the texture set", s->batches[i].texture);
            part.texture = render_texture(NULL);
        }
        render_mesh_add_part(&v.bricks, &part);
        at += part.vertex_count;
    }
}

/*
 * The background is a sphere seen from inside. The original draws it with
 * back faces culled (0x41b72c), so with the camera zoomed out past it only
 * its far side shows and the board stays in view.
 */
static void build_background_mesh(void)
{
    int i;

    render_mesh_destroy(&v.background);
    build_model_mesh(&v.background, &v.scene.background_model,
                     0, v.scene.background_texture, "", NULL);
    /* The sky sphere is lit like everything else, but fon.x scales it up
       2.5 times in its frame, and Direct3D lights it with normals shrunk
       by as much (the inverse transpose, not normalised), so only its
       lower part, facing up at the light, is lit at all, and dimly. */
    for (i = 0; i < v.background.part_count; i++) {
        const XModel *m = &v.scene.background_model;
        const XPart *part = &m->parts[i < m->part_count ? i : 0];
        if (part->frame_index >= 0) {
            Mat4 b = m->frames[part->frame_index].bind;
            float scale = cbrtf(fabsf(b.m[0][0] * (b.m[1][1] * b.m[2][2] - b.m[1][2] * b.m[2][1]) -
                                      b.m[0][1] * (b.m[1][0] * b.m[2][2] - b.m[1][2] * b.m[2][0]) +
                                      b.m[0][2] * (b.m[1][0] * b.m[2][1] - b.m[1][1] * b.m[2][0])));
            if (scale > 0.0f)
                v.background.parts[i].normal_length = 1.0f / scale;
        }
    }
}

static void build_board_mesh(void)
{
    render_mesh_destroy(&v.board);
    build_model_mesh(&v.board, &v.scene.board_model, 0, v.scene.board_texture, "Doska", NULL);
}

static void reset_camera(void)
{
    v.camera.look_z = v.scene.board.camera_look_z;
    v.camera.distance = v.scene.board.camera_distance;
    v.camera.zoom = 1.0f;
    v.camera.yaw = 0.0f;
    v.camera.pitch = 0.8727f; /* 50 degrees */
    v.camera.roll = 0.05f;
}

/*
 * The bat model holds every look the bat can have: the plain bat is the
 * body (with its turbines and fans), the hands and the plate. The magnet
 * replaces the plate, and the guns, bazooka and cannon are extra frames
 * (0x404d62).
 */
static bool plain_bat_part(const XPart *part)
{
    return xpart_in_frame(part, "Corpus") || xpart_in_frame(part, "L_Hand") ||
           xpart_in_frame(part, "R_Hand") || xpart_in_frame(part, "Plashka");
}

static bool in_any_frame(const XPart *part, const char *const *frames)
{
    for (; *frames != NULL; frames++)
        if (xpart_in_frame(part, *frames))
            return true;
    return false;
}

static const char *const k_gun_frames[] = { "L_Pulemet", "L_Priklad", "L_Wing", "R_Pulemet",
                                            "R_Priklad", "R_Wing", NULL };
static const char *const k_bazooka_frames[] = { "L_Stvol", "L_Derjatel", "L_Wing", "R_Stvol",
                                                "R_Derjatel", "R_Wing", NULL };
static const char *const k_cannon_frames[] = { "For_Pushka", "Pushka", NULL };

/* The frames a weapon adds to the bat, NULL for none. */
static const char *const *weapon_frames(BatMode mode)
{
    switch (mode) {
    case BAT_GUN: return k_gun_frames;
    case BAT_BAZOOKA: return k_bazooka_frames;
    case BAT_CANNON: return k_cannon_frames;
    default: return NULL;
    }
}

/* Is this part of the bat drawn now? The magnet takes the plate's place. */
static bool bat_part_shown(const XPart *part, BatMode mode)
{
    const char *const *weapon = weapon_frames(mode);

    if (xpart_in_frame(part, "Magnit"))
        return mode == BAT_MAGNET;
    if (xpart_in_frame(part, "Plashka"))
        return mode != BAT_MAGNET;
    return plain_bat_part(part) || (weapon != NULL && in_any_frame(part, weapon));
}

/* The bat model named by the level's pad file ("grdata"). */
/* Pose every frame of a model at one time, as objects do with their age (0x401960). */
static Mat4 *pose_model(const XModel *m, unsigned time)
{
    unsigned *times = malloc((size_t)(m->frame_count + 1) * sizeof(*times));
    Mat4 *pose = malloc((size_t)(m->part_count + 1) * sizeof(*pose));
    int i;

    if (times == NULL || pose == NULL) {
        free(times);
        free(pose);
        return NULL;
    }
    for (i = 0; i < m->frame_count; i++)
        times[i] = time;
    xmodel_pose(m, times, pose);
    free(times);
    return pose;
}

/* Set a frame's animation time and its children's, as 0x421a90 does. */
static void set_frame_time(const XModel *m, unsigned *times, const char *name, unsigned time)
{
    int frame = xmodel_find_frame(m, name), i;

    if (frame < 0)
        return;
    for (i = frame; i < m->frame_count; i++) {
        int up = i;
        while (up > frame)
            up = m->frames[up].parent;
        if (up == frame)
            times[i] = time;
    }
}

/*
 * The bat's width and look (0x404d62): the plate (or the magnet, which
 * takes its place) is stretched to the bat's width, 3 being the model's
 * own; the hands only move in when the bat is narrower than that.
 */
static void shape_bat(Mat4 *pose)
{
    const XModel *m = &v.bat_model;
    float stretch = v.play.bat.half_width / 3.0f;
    int p;

    for (p = 0; p < m->part_count; p++) {
        const XPart *part = &m->parts[p];
        if (!bat_part_shown(part, v.play.bat.mode))
            pose[p] = mat4_scaling(0.0f, 0.0f, 0.0f);
        else if (xpart_in_frame(part, "Plashka") || xpart_in_frame(part, "Magnit") ||
                 (stretch < 1.0f && (xpart_in_frame(part, "L_Hand") || xpart_in_frame(part, "R_Hand"))))
            pose[p] = mat4_multiply(pose[p], mat4_scaling(stretch, 1.0f, 1.0f));
    }
}

/*
 * The fans spin on the game clock; while the bat leans, the turbines take
 * the lean's time. Setting a frame's time sets its children's too, so the
 * turbines go first and the fans inside keep spinning, as they do in the
 * original game.
 */
static Mat4 *pose_bat(void)
{
    const XModel *m = &v.bat_model;
    unsigned *times = malloc((size_t)(m->frame_count + 1) * sizeof(*times));
    Mat4 *pose = malloc((size_t)(m->part_count + 1) * sizeof(*pose));
    int i;

    if (times == NULL || pose == NULL) {
        free(times);
        free(pose);
        return NULL;
    }
    for (i = 0; i < m->frame_count; i++)
        times[i] = XFRAME_REST;
    if (v.turbine_time != XFRAME_REST) {
        set_frame_time(m, times, "L_Turbine", v.turbine_time);
        set_frame_time(m, times, "R_Turbine", v.turbine_time);
    }
    set_frame_time(m, times, "R_Fan", (unsigned)v.time);
    set_frame_time(m, times, "L_Fan", (unsigned)v.time);
    /* The bounce when a ball hits it (0x405218), except with the magnet,
       which holds its hands still (0x404653). */
    {
        bool magnet = v.play.bat.mode == BAT_MAGNET;
        unsigned bounce = magnet ? 0 : v.play.bat.bounce_time;
        set_frame_time(m, times, "Magnit", 0);
        set_frame_time(m, times, "Plashka", bounce);
        set_frame_time(m, times, "L_Hand", bounce);
        set_frame_time(m, times, "R_Hand", bounce);
        set_frame_time(m, times, "L_Ressore", bounce);
        set_frame_time(m, times, "R_Ressore", bounce);
    }
    /* The weapon's frames come out and recoil (0x4052a7). */
    {
        const char *const *weapon = weapon_frames(v.play.bat.mode);
        for (; weapon != NULL && *weapon != NULL; weapon++)
            set_frame_time(m, times, *weapon, v.play.bat.weapon_time);
    }
    xmodel_pose(m, times, pose);
    free(times);
    shape_bat(pose);
    return pose;
}

static void load_model(const char *path, XModel *model, RenderMesh *mesh)
{
    if (xfile_load(model, path))
        build_model_mesh(mesh, model, 0, "", "", NULL);
    else
        log_warn("can't load the model %s", path);
}

/* Each kind of prize's model, named by its file, loaded the first time it is needed. */
static void load_prize_models(void)
{
    int k;

    for (k = PRIZE_NONE + 1; k < PRIZE_KINDS; k++) {
        const PrizeInfo *info = &v.play.prize_info[k];
        char path[96];

        if (v.prize_loaded[k] || !info->loaded || info->model[0] == '\0')
            continue;
        v.prize_loaded[k] = true;
        snprintf(path, sizeof(path), "SkinMeshes/%s", info->model);
        load_model(path, &v.prize_models[k], &v.prize_meshes[k]);
    }
}

/* What the bat's weapons fire, as the level's pad file says. */
static void load_bullet_models(void)
{
    int w;

    for (w = 0; w < SHOT_KINDS; w++) {
        char path[96];

        render_mesh_destroy(&v.bullet_meshes[w]);
        xfile_free(&v.bullet_models[w]);
        if (v.play.bullet_info[w].model[0] == '\0')
            continue;
        snprintf(path, sizeof(path), "SkinMeshes/%s", v.play.bullet_info[w].model);
        load_model(path, &v.bullet_models[w], &v.bullet_meshes[w]);
    }
}

/* The model for each kind of animal in the level. */
static void load_animal_models(void)
{
    int i, k;

    for (i = 0; i < v.scene.brick_count; i++) {
        const SceneAnimal *animal = v.scene.bricks[i].animal;
        char path[96];

        if (animal == NULL)
            continue;
        for (k = 0; k < v.animal_count && v.animals[k].animal != animal; k++)
            ;
        if (k < v.animal_count || v.animal_count >= (int)(sizeof(v.animals) / sizeof(v.animals[0])))
            continue;
        v.animals[k].animal = animal;
        v.animal_count++;
        snprintf(path, sizeof(path), "SkinMeshes/%s", animal->model);
        load_model(path, &v.animals[k].model, &v.animals[k].mesh);
    }
}

static int animal_model(const SceneAnimal *animal)
{
    int k;

    for (k = 0; k < v.animal_count; k++)
        if (v.animals[k].animal == animal)
            return k;
    return -1;
}

/*
 * Animals play their own animation from a random start (0x420c50), at
 * real speed (the original shows them slower than the object clock).
 * One flying off also rolls about its own z axis.
 */
static void draw_animals(RenderPass kind, float t)
{
    int i;

    for (i = 0; i < v.scene.brick_count; i++) {
        const SceneBrick *b = &v.scene.bricks[i];
        int k = b->animal != NULL ? animal_model(b->animal) : -1;
        Mat4 world, *pose;
        Vec3 p = b->position;

        if (k < 0 || (b->removed && b->flying_ms == 0))
            continue;
        if (b->animal->kind == MODEL_TRAIN) /* faces along its track (0x408c9f) */
            world = mat4_multiply(mat4_rotation_y(b->heading - 1.5707964f), mat4_translation(p.x, p.y, p.z));
        else
            world = scene_brick_matrix(b);
        if (b->flip != 0.0f) {
            world = mat4_multiply(world, mat4_translation(-p.x, -p.y, -p.z));
            world = mat4_multiply(world, mat4_rotation_z(b->flip));
            world = mat4_multiply(world, mat4_translation(p.x, p.y, p.z));
        }
        if (b->drop != 0.0f)
            world = mat4_multiply(world, mat4_translation(0.0f, -b->drop, 0.0f));
        /* A cannon stands still between shots (0x4077b0). */
        pose = pose_model(&v.animals[k].model,
                          b->animal->kind == MODEL_CANNON ? b->animation_ms : b->animation_ms + v.time);
        render_draw_posed(&v.animals[k].mesh, world, pose, kind, t);
        free(pose);
    }
}

static void load_bat_model(void)
{
    char path[128];
    const char *pad = v.scene.level.slots[LEVEL_SLOT_PAD];
    Config cfg;

    xfile_free(&v.bat_model);
    render_mesh_destroy(&v.bat);
    snprintf(path, sizeof(path), "SkinMeshes/pad.x");
    if (config_load(&cfg, pad[0] ? pad : "Pads/common.txt")) {
        snprintf(path, sizeof(path), "SkinMeshes/%s", config_get_string(&cfg, "grdata", "pad.x"));
        config_free(&cfg);
    }
    v.turbine_time = XFRAME_REST;
    if (xfile_load(&v.bat_model, path))
        build_model_mesh(&v.bat, &v.bat_model, 0, "", "", NULL);
    else
        log_warn("can't load the bat model %s", path);
}

/*
 * A train's engine (Train1.ogg) loops from the moment it is made until it
 * is gone, panned to follow it while it runs (0x4089d9, 0x408d4a).
 */
static void train_sounds(bool stop_all)
{
    int i;

    for (i = 0; i < v.scene.brick_count && i < (int)(sizeof(v.train_sounds) / sizeof(v.train_sounds[0])); i++) {
        const SceneBrick *b = &v.scene.bricks[i];
        bool there = !stop_all && b->animal != NULL && b->animal->kind == MODEL_TRAIN &&
                     !(b->removed && b->flying_ms == 0);
        int pan = (int)(sinf(b->heading) * 100.0f);

        if (!there) {
            if (v.train_sounds[i] != 0)
                audio_stop_loop(v.train_sounds[i]);
            v.train_sounds[i] = 0;
        } else if (v.train_sounds[i] == 0) {
            v.train_sounds[i] = audio_play_loop("Train1.ogg", -1, pan);
        } else if (!b->removed) {
            audio_pan_loop(v.train_sounds[i], pan);
        }
    }
}

static void unload_level(void)
{
    train_sounds(true);
    render_mesh_destroy(&v.background);
    render_mesh_destroy(&v.board);
    render_mesh_destroy(&v.bricks);
    if (v.playing)
        play_free(&v.play);
    v.playing = false;
    if (v.scene_loaded)
        scene_free(&v.scene);
    v.scene_loaded = false;
}

static void play_sound(const char *name, int volume, int pan, void *user)
{
    (void)user;
    audio_play(name, volume, pan);
}

static void update_title(void)
{
    char title[320];
    const char *state = "";

    if (!v.playing)
        return;
    switch (v.play.state) {
    case PLAY_LEVEL_DONE: state = "  -  level complete!"; break;
    case PLAY_GAME_OVER: state = "  -  game over (click to try again)"; break;
    default: break;
    }
    snprintf(title, sizeof(title), "OpenMB2 - Score %d   Lives %d   Level %d   %s%s",
             v.play.score, v.play.lives, v.level + 1, v.packs[v.pack].levels[v.level], state);
    if (strcmp(title, v.title) != 0) {
        snprintf(v.title, sizeof(v.title), "%s", title);
        SDL_SetWindowTitle(v.window, title);
    }
}

/* The level tunes are Music/mb0.txt to mb3.txt (0x41474f, 0x41c00a). */
static void play_level_music(int tune)
{
    char path[32];
    v.music = (tune % 4 + 4) % 4;
    snprintf(path, sizeof(path), "Music/mb%d.txt", v.music);
    audio_play_music(path);
}

static void load_level(int pack, int level)
{
    const Pack *p;
    char path[256];
    const TextureSet *ts;

    unload_level();
    if (v.pack_count == 0)
        return;
    v.pack = (pack % v.pack_count + v.pack_count) % v.pack_count;
    p = &v.packs[v.pack];
    v.level = (level % p->level_count + p->level_count) % p->level_count;

    snprintf(path, sizeof(path), "%s/%s", p->folder, p->levels[v.level]);
    if (!scene_load(&v.scene, path, rand())) {
        log_error("can't load level %s", path);
        return;
    }
    load_animal_models();
    v.object_time = 0;
    v.scene_loaded = true;

    build_background_mesh();
    build_board_mesh();
    build_brick_mesh();
    load_bat_model();

    ts = &v.scene.textures;
    memcpy(v.light.direction, ts->light_dir, sizeof(v.light.direction));
    memcpy(v.light.ambient, ts->ambient, sizeof(v.light.ambient));
    memcpy(v.light.diffuse, ts->diffuse, sizeof(v.light.diffuse));
    reset_camera();
    /* Like the original, each level starts one of the four tunes at random. */
    play_level_music(rand());

    if (play_start(&v.play, &v.scene, v.score, v.lives, v.options.mouse_sensitivity)) {
        v.play.sound = play_sound;
        v.playing = true;
        load_prize_models();
        load_bullet_models();
        if (v.firework_mesh.buffer == 0)
            load_model("SkinMeshes/firework.x", &v.firework_model, &v.firework_mesh);
    }
    log_info("level %s: %d bricks (%d to break), %d other objects, board %s", path,
             v.scene.brick_count, v.play.bricks_left, v.scene.other_count,
             v.scene.level.slots[LEVEL_SLOT_BOARD]);
    update_title();
}

/* ------------------------------------------------------------------------ */

bool play_screen_init(SDL_Window *window, const PlayScreenOptions *options)
{
    const char *start_level = options->start_level;
    int pack = 0, level = 0, i, j;

    memset(&v, 0, sizeof(v));
    v.window = window;
    v.options = *options;
    v.lives = START_LIVES;
    if (!render_init())
        return false;
    find_packs();
    if (v.pack_count == 0) {
        log_error("no level packs found");
        return false;
    }

    if (start_level != NULL) {
        char wanted[256], candidate[256];
        vfs_normalise(start_level, wanted, sizeof(wanted));
        for (i = 0; i < v.pack_count; i++) {
            for (j = 0; j < v.packs[i].level_count; j++) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%s", v.packs[i].folder, v.packs[i].levels[j]);
                vfs_normalise(path, candidate, sizeof(candidate));
                if (strcmp(candidate, wanted) == 0) {
                    pack = i;
                    level = j;
                }
            }
        }
    }
    load_model("SkinMeshes/s_ball.x", &v.ball_model, &v.ball);
    load_model("SkinMeshes/s_explosive.x", &v.explosive_model, &v.explosive_ball);
    load_model("SkinMeshes/s_gipnoz.x", &v.hypnotic_model, &v.hypnotic_ball);
    v.test_prize = PRIZE_NONE + 1;
    v.particle_texture = texture_for_name("particle.png");
    v.bolt_texture = texture_for_name("lighting.png");
    v.rubber_texture = texture_for_name("rubber.png");
    v.explosion_texture = texture_for_name("explose.png");
    v.shadow_texture = texture_for_name("bshadow.png");
    v.glow_texture = texture_for_name("infravis.png");
    v.billboard_texture = texture_for_name("Billboards.png");
    load_level(pack, level);
    return true;
}

void play_screen_shutdown(void)
{
    int i;
    unload_level();
    render_mesh_destroy(&v.bat);
    render_mesh_destroy(&v.explosions);
    render_mesh_destroy(&v.ball);
    render_mesh_destroy(&v.explosive_ball);
    render_mesh_destroy(&v.hypnotic_ball);
    render_mesh_destroy(&v.shadows);
    render_mesh_destroy(&v.glows);
    render_mesh_destroy(&v.prize_glows);
    render_mesh_destroy(&v.trail);
    render_mesh_destroy(&v.bolts);
    render_mesh_destroy(&v.bullet_shadows);
    render_mesh_destroy(&v.strings);
    render_mesh_destroy(&v.billboards);
    free(unblended.bricks);
    memset(&unblended, 0, sizeof(unblended));
    for (i = 0; i < SHOT_KINDS; i++) {
        render_mesh_destroy(&v.bullet_meshes[i]);
        xfile_free(&v.bullet_models[i]);
    }
    render_mesh_destroy(&v.firework_mesh);
    xfile_free(&v.firework_model);
    xfile_free(&v.bat_model);
    xfile_free(&v.ball_model);
    xfile_free(&v.explosive_model);
    xfile_free(&v.hypnotic_model);
    for (i = 0; i < v.animal_count; i++) {
        render_mesh_destroy(&v.animals[i].mesh);
        xfile_free(&v.animals[i].model);
    }
    v.animal_count = 0;
    for (i = 0; i < PRIZE_KINDS; i++) {
        render_mesh_destroy(&v.prize_meshes[i]);
        xfile_free(&v.prize_models[i]);
    }
    for (i = 0; i < v.pack_count; i++)
        free(v.packs[i].levels);
    render_shutdown();
    memset(&v, 0, sizeof(v));
}

/* Testing levels by hand: jump straight to another one, with a new game. */
static void jump_to_level(int pack, int level)
{
    v.score = 0;
    v.lives = START_LIVES;
    load_level(pack, level);
}

void play_screen_handle_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_KEYDOWN:
        switch (event->key.keysym.sym) {
        case SDLK_RIGHT: jump_to_level(v.pack, v.level + 1); break;
        case SDLK_LEFT: jump_to_level(v.pack, v.level - 1); break;
        case SDLK_PAGEDOWN: jump_to_level(v.pack + 1, 0); break;
        case SDLK_PAGEUP: jump_to_level(v.pack - 1, 0); break;
        case SDLK_SPACE: reset_camera(); break;
        case SDLK_F9: /* testing: drop the next kind of prize in front of the bat */
            if (v.playing && v.play.state == PLAY_RUNNING) {
                play_drop_prize(&v.play, (PrizeKind)v.test_prize,
                                vec3(v.play.bat.position.x, 0.0f, v.play.bat.position.z + 12.0f));
                v.test_prize = v.test_prize % (PRIZE_KINDS - 1) + 1;
            }
            break;
        case SDLK_F10: /* OpenMB2's own: smooth motion on or off */
            v.options.smooth = !v.options.smooth;
            v.play.bricks_changed = true; /* bake them where they really are */
            log_info("smooth motion %s", v.options.smooth ? "on" : "off");
            break;
        case SDLK_F6: /* like the original: the next tune */
            play_level_music(v.music + 1);
            break;
        case SDLK_F7: /* like the original: a new random background */
            if (v.scene_loaded) {
                scene_randomize_background(&v.scene, rand());
                build_background_mesh();
            }
            break;
        case SDLK_F8: /* and a new random board floor */
            if (v.scene_loaded) {
                scene_randomize_board(&v.scene, rand());
                build_board_mesh();
            }
            break;
        default: break;
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_RIGHT) {
            v.dragging = true;
        } else if (event->button.button == SDL_BUTTON_LEFT && v.playing) {
            if (v.play.state == PLAY_GAME_OVER)
                jump_to_level(v.pack, v.level);
            else {
                play_click(&v.play);
                play_hold_trigger(&v.play, true);
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_RIGHT)
            v.dragging = false;
        else if (event->button.button == SDL_BUTTON_LEFT)
            play_hold_trigger(&v.play, false);
        break;
    case SDL_MOUSEMOTION:
        if (v.dragging) {
            /* The original turns the camera by 0.005 per mouse step, yaw
               within ±60° and pitch from 15° to 90° (0x409ed0, 0x409e80). */
            v.camera.yaw += (float)event->motion.xrel * 0.005f;
            v.camera.pitch += (float)event->motion.yrel * 0.005f;
            if (v.camera.yaw < -1.0472f) v.camera.yaw = -1.0472f;
            if (v.camera.yaw > 1.0472f) v.camera.yaw = 1.0472f;
            if (v.camera.pitch < 0.2618f) v.camera.pitch = 0.2618f;
            if (v.camera.pitch > 1.5708f) v.camera.pitch = 1.5708f;
        } else if (v.playing) {
            play_mouse_move(&v.play, event->motion.xrel);
        }
        break;
    case SDL_MOUSEWHEEL:
        /* Zoom stays within 0.25 to 1.75 of the board's distance (0x409f20).
           The original moves out with the wheel forward, by a step too small
           to notice; here forward moves in, by 0.1 a notch. */
        v.camera.zoom -= (float)event->wheel.y * 0.1f;
        if (v.camera.zoom < 0.25f) v.camera.zoom = 0.25f;
        if (v.camera.zoom > 1.75f) v.camera.zoom = 1.75f;
        break;
    default:
        break;
    }
}

/*
 * Colour-changing (0x10) and explosive (0x20) bricks step their texture
 * down one row of the sheet every 100 ms of the object clock, round all
 * of its rows (0x407880).
 */
static bool flash_bricks(uint32_t object_ms)
{
    bool changed = false;
    int i;

    for (i = 0; i < v.scene.brick_count; i++) {
        SceneBrick *b = &v.scene.bricks[i];
        const TextureScript *script;
        int rows, flash;

        if (b->removed || (b->object->type != 0x10 && b->object->type != 0x20 && !b->shining))
            continue;
        script = textureset_get(&v.scene.textures, b->texture);
        rows = script && script->yfactor > 0.0f ? (int)(1.0f / script->yfactor) : 1;
        if (b->shining) {
            /* Rows 0 to rows, 100 ms each, then back to the first. */
            b->shine_ms += object_ms;
            flash = (int)(b->shine_ms / 100u);
            if (flash > rows) {
                flash = 0;
                b->shining = false;
                b->shine_ms = 0;
            }
        } else {
            flash = (int)((v.object_time / 100u) % (uint32_t)(rows > 0 ? rows : 1));
        }
        if (b->flash != flash) {
            b->flash = flash;
            changed = true;
        }
    }
    return changed;
}

static void update(uint32_t ms)
{
    if (flash_bricks(v.playing ? ms * 2 : ms)) {
        if (v.playing) {
            v.play.bricks_changed = true;
        } else {
            scene_bake_bricks(&v.scene);
            build_brick_mesh();
        }
    }
    if (!v.playing)
        return;
    if (v.options.autoplay)
        play_autopilot(&v.play);
    play_update(&v.play, ms);
    train_sounds(false);
    if (v.play.bricks_changed) {
        scene_bake_bricks(&v.scene);
        build_brick_mesh();
        v.play.bricks_changed = false;
    }
    if (v.play.state == PLAY_LEVEL_DONE && v.play.state_time >= LEVEL_DONE_MS) {
        /* Next level; lives carry over (no extra life: Lives stays the same in the original). */
        v.score = v.play.score;
        v.lives = v.play.lives;
        load_level(v.pack, v.level + 1);
        return;
    }
    update_title();
}

/*
 * The flashes where bricks broke: camera-facing squares 4 units across,
 * each showing one of the 16 frames of explose.png (0x406030).
 */
static void build_explosion_mesh(Mat4 view)
{
    const Scene *s = &v.scene;
    Vec3 right = vec3(view.m[0][0], view.m[1][0], view.m[2][0]);
    Vec3 up = vec3(view.m[0][1], view.m[1][1], view.m[2][1]);
    XVertex quads[SCENE_MAX_EXPLOSIONS * 6];
    static const float corners[6][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    RenderPart part;
    int i, k, n = 0;

    render_mesh_destroy(&v.explosions);
    if (s->explosion_count == 0)
        return;
    for (i = 0; i < s->explosion_count; i++) {
        const SceneExplosion *e = &s->explosions[i];
        int frame = (int)(e->age / 75);
        float u0 = (float)(frame >> 2) * 0.25f, v0 = (float)(frame & 3) * 0.25f;

        if (frame > 15)
            frame = 15;
        for (k = 0; k < 6; k++) {
            XVertex *q = &quads[n++];
            float cx = corners[k][0], cy = corners[k][1];
            Vec3 p = vec3_add(e->position, vec3_add(vec3_scale(right, 2.0f * cx), vec3_scale(up, 2.0f * cy)));
            q->pos[0] = p.x;
            q->pos[1] = p.y;
            q->pos[2] = p.z;
            q->normal[0] = 0.0f;
            q->normal[1] = 0.1f;
            q->normal[2] = 1.0f;
            q->uv[0] = u0 + (cx + 1.0f) * 0.125f;
            q->uv[1] = v0 + (1.0f - cy) * 0.125f;
        }
    }
    if (!render_mesh_create(&v.explosions, quads, n))
        return;
    memset(&part, 0, sizeof(part));
    part.vertex_count = n;
    part.texture = v.explosion_texture;
    part.diffuse[0] = part.diffuse[1] = part.diffuse[2] = part.diffuse[3] = 1.0f;
    part.flags = RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_NOT_LIGHTED | RENDER_TWO_SIDED;
    render_mesh_add_part(&v.explosions, &part);
}

/*
 * A blown-up bat (0x404cb6): each of these frames is drawn on its own,
 * spun about its piece's axis and moved by the piece's offset, all faded
 * by the time left.
 */
static void draw_bat_pieces(Mat4 bat_matrix, const Mat4 *pose, float t)
{
    static const char *const names[BAT_PIECES] = {
        "Corpus", "Plashka", "L_Ressore", "R_Ressore", "L_Hand",
        "R_Hand", "L_Turbine", "R_Turbine", "L_Fan", "R_Fan"
    };
    const XModel *m = &v.bat_model;
    const Bat *bat = &v.play.bat;
    Mat4 *only = malloc((size_t)(m->part_count + 1) * sizeof(*only));
    int i, p;

    if (only == NULL || pose == NULL) {
        free(only);
        return;
    }
    render_set_fade((float)bat->pieces_ms * (1.0f / 1500.0f));
    for (i = 0; i < BAT_PIECES; i++) {
        const BatPiece *piece = &bat->pieces[i];
        Mat4 world = mat4_multiply(
            mat4_multiply(mat4_rotation_axis(vec3_normalize(piece->axis), piece->angle),
                          mat4_translation(piece->position.x, piece->position.y, piece->position.z)),
            bat_matrix);
        /* Parts outside this frame are squashed to nothing. */
        for (p = 0; p < m->part_count; p++)
            only[p] = strcmp(m->parts[p].frame, names[i]) == 0 ? pose[p] : mat4_scaling(0, 0, 0);
        render_draw_posed(&v.bat, world, only, RENDER_PASS_ALL, t);
    }
    render_set_fade(1.0f);
    free(only);
}

/* One quad as two triangles, from four corners in order around it. */
static void add_quad(XVertex *out, const Vec3 *corner)
{
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int k;

    for (k = 0; k < 6; k++) {
        XVertex *q = &out[k];
        q->pos[0] = corner[order[k]].x;
        q->pos[1] = corner[order[k]].y;
        q->pos[2] = corner[order[k]].z;
        q->normal[0] = 0.0f;
        q->normal[1] = 1.0f;
        q->normal[2] = 0.0f;
        q->uv[0] = uv[order[k]][0];
        q->uv[1] = uv[order[k]][1];
    }
}

static void make_overlay(RenderMesh *mesh, const XVertex *quads, int n, unsigned texture,
                         uint32_t flags)
{
    RenderPart part;

    render_mesh_destroy(mesh);
    if (n == 0 || !render_mesh_create(mesh, quads, n))
        return;
    memset(&part, 0, sizeof(part));
    part.vertex_count = n;
    part.texture = texture;
    part.diffuse[0] = part.diffuse[1] = part.diffuse[2] = part.diffuse[3] = 1.0f;
    part.flags = flags | RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_NOT_LIGHTED | RENDER_TWO_SIDED;
    part.pose = -1;
    render_mesh_add_part(mesh, &part);
}

/*
 * The game moves things in steps of 18 ms. On a faster screen, frames
 * between steps would show them standing still, so for drawing each is
 * put part way from where it was before the last step to where it is
 * now, and put back afterwards. A ball on the bat goes with the bat,
 * which the mouse moves between steps. Falling, tipping and shrinking
 * bricks are blended too, which means baking the bricks again; a brick
 * that finished tipping in the last step has jumped to its new place and
 * is drawn there.
 */

static bool brick_moved(int i)
{
    const SceneBrick *b = &v.scene.bricks[i];
    const PlayBrick *pb = &v.play.bricks[i];

    return !b->removed && b->roll == pb->roll_from &&
           (b->position.x != pb->from.x || b->position.y != pb->from.y ||
            b->tip != pb->tip_from || b->scale != pb->scale_from);
}

/* Blends the bricks that moved in the last step, and bakes them if any did. */
static void blend_bricks(float t)
{
    int i;
    bool any = false;

    unblended.bricks_saved = false;
    if (unblended.brick_capacity < v.scene.brick_count) {
        UnblendedBrick *grown = realloc(unblended.bricks, sizeof(*grown) * (size_t)v.scene.brick_count);
        if (grown == NULL)
            return;
        unblended.bricks = grown;
        unblended.brick_capacity = v.scene.brick_count;
    }
    for (i = 0; i < v.scene.brick_count; i++) {
        SceneBrick *b = &v.scene.bricks[i];
        const PlayBrick *pb = &v.play.bricks[i];
        unblended.bricks[i].position = b->position;
        unblended.bricks[i].tip = b->tip;
        unblended.bricks[i].scale = b->scale;
        unblended.bricks[i].heading = b->heading;
        unblended.bricks_saved = true;
        if (b->animal != NULL) { /* drawn as a model, not baked */
            b->position = vec3_add(pb->from, vec3_scale(vec3_sub(b->position, pb->from), t));
            b->heading = pb->heading_from + (b->heading - pb->heading_from) * t;
            continue;
        }
        if (!brick_moved(i))
            continue;
        b->position = vec3_add(pb->from, vec3_scale(vec3_sub(b->position, pb->from), t));
        b->tip = pb->tip_from + (b->tip - pb->tip_from) * t;
        b->scale = pb->scale_from + (b->scale - pb->scale_from) * t;
        any = true;
    }
    /* Bake when something is blended, and once more after, to leave it where it is. */
    if (any || unblended.bricks_blended) {
        scene_bake_bricks(&v.scene);
        build_brick_mesh();
    }
    unblended.bricks_blended = any;
}

static Vec3 vec3_lerp(Vec3 a, Vec3 b, float t)
{
    return vec3_add(a, vec3_scale(vec3_sub(b, a), t));
}

static void blend_positions(void)
{
    Play *play = &v.play;
    float t = play_blend(play);
    int i;

    for (i = 0; i < play->ball_count; i++) {
        Ball *ball = &play->balls[i];
        unblended.balls[i] = ball->position;
        unblended.spins[i] = ball->spin;
        ball->position = vec3_lerp(ball->from, ball->position, t);
        ball->spin = ball->spin_from + (ball->spin - ball->spin_from) * t;
        if (ball->flags & BALL_STUCK)
            ball->position.x = play->bat.position.x + ball->stuck_offset;
    }
    for (i = 0; i < play->prize_count; i++) {
        unblended.prizes[i] = play->prizes[i].position;
        play->prizes[i].position = vec3_lerp(play->prizes[i].from, play->prizes[i].position, t);
    }
    for (i = 0; i < play->bullet_count; i++) {
        unblended.bullets[i] = play->bullets[i].position;
        play->bullets[i].position = vec3_lerp(play->bullets[i].from, play->bullets[i].position, t);
    }
    for (i = 0; i < BAT_PIECES; i++) {
        unblended.pieces[i] = play->bat.pieces[i].position;
        play->bat.pieces[i].position = vec3_lerp(play->bat.pieces[i].from, play->bat.pieces[i].position, t);
    }
    for (i = 0; i < play->particle_count; i++) {
        unblended.particles[i] = play->particles[i].position;
        play->particles[i].position = vec3_lerp(play->particles[i].from, play->particles[i].position, t);
    }
    for (i = 0; i < play->smoke_count; i++) {
        unblended.smoke[i] = play->smoke[i].position;
        play->smoke[i].position = vec3_lerp(play->smoke[i].from, play->smoke[i].position, t);
    }
    for (i = 0; i < play->spark_count; i++) {
        unblended.sparks[i] = play->sparks[i].position;
        play->sparks[i].position = vec3_lerp(play->sparks[i].from, play->sparks[i].position, t);
    }
    for (i = 0; i < play->firework_count; i++) {
        unblended.fireworks[i] = play->fireworks[i].position;
        play->fireworks[i].position = vec3_lerp(play->fireworks[i].from, play->fireworks[i].position, t);
    }
    blend_bricks(t);
}

static void unblend_positions(void)
{
    Play *play = &v.play;
    int i;

    for (i = 0; i < play->ball_count; i++) {
        play->balls[i].position = unblended.balls[i];
        play->balls[i].spin = unblended.spins[i];
    }
    for (i = 0; i < play->prize_count; i++)
        play->prizes[i].position = unblended.prizes[i];
    for (i = 0; i < play->bullet_count; i++)
        play->bullets[i].position = unblended.bullets[i];
    for (i = 0; i < BAT_PIECES; i++)
        play->bat.pieces[i].position = unblended.pieces[i];
    for (i = 0; i < play->particle_count; i++)
        play->particles[i].position = unblended.particles[i];
    for (i = 0; i < play->smoke_count; i++)
        play->smoke[i].position = unblended.smoke[i];
    for (i = 0; i < play->spark_count; i++)
        play->sparks[i].position = unblended.sparks[i];
    for (i = 0; i < play->firework_count; i++)
        play->fireworks[i].position = unblended.fireworks[i];
    for (i = 0; unblended.bricks_saved && i < v.scene.brick_count; i++) {
        v.scene.bricks[i].position = unblended.bricks[i].position;
        v.scene.bricks[i].tip = unblended.bricks[i].tip;
        v.scene.bricks[i].scale = unblended.bricks[i].scale;
        v.scene.bricks[i].heading = unblended.bricks[i].heading;
    }
}

/* A camera-facing square of half size (w, h) around p. */
static void add_billboard(XVertex *out, Mat4 view, Vec3 p, float w, float h)
{
    Vec3 right = vec3(view.m[0][0], view.m[1][0], view.m[2][0]);
    Vec3 up = vec3(view.m[0][1], view.m[1][1], view.m[2][1]);
    Vec3 c[4];

    c[0] = vec3_add(p, vec3_add(vec3_scale(right, -w), vec3_scale(up, h)));
    c[1] = vec3_add(p, vec3_add(vec3_scale(right, w), vec3_scale(up, h)));
    c[2] = vec3_add(p, vec3_add(vec3_scale(right, w), vec3_scale(up, -h)));
    c[3] = vec3_add(p, vec3_add(vec3_scale(right, -w), vec3_scale(up, -h)));
    add_quad(out, c);
}

/*
 * Fire and fountains (FireBrick / WaterBrick draw): an eight frame
 * animation from Billboards.png, a 4x4 grid, fire in the left half and
 * water in the right, one frame every 230 ms of their clock. Each faces the camera,
 * 2 wide, from 1 above its cell to 1 below, and is blended without
 * depth writes (t_billboard).
 */
static void build_billboards(Mat4 view)
{
    XVertex *quads;
    Vec3 right = vec3(view.m[0][0], view.m[1][0], view.m[2][0]);
    Vec3 up = vec3(view.m[0][1], view.m[1][1], view.m[2][1]);
    unsigned frame = (v.object_time / 230u) & 7u;
    int i, k, n = v.scene.billboard_count;

    render_mesh_destroy(&v.billboards);
    if (n == 0)
        return;
    quads = malloc(sizeof(*quads) * (size_t)n * 6);
    if (!quads)
        return;
    for (i = 0; i < n; i++) {
        const SceneBillboard *b = &v.scene.billboards[i];
        Vec3 p = vec3(b->position.x, b->position.y - 0.5f, b->position.z), c[4];
        float u0 = (float)(frame >> 2) * 0.25f + (b->water ? 0.5f : 0.0f);
        float v0 = (float)(frame & 3) * 0.25f + 1.0f / 512.0f;
        float u1 = u0 + 0.25f, v1 = v0 + 0.25f - 1.0f / 256.0f;
        const float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
        static const int order[6] = { 0, 1, 2, 0, 2, 3 };

        c[0] = vec3_add(p, vec3_add(vec3_scale(right, -1.0f), vec3_scale(up, 1.5f)));
        c[1] = vec3_add(p, vec3_add(vec3_scale(right, 1.0f), vec3_scale(up, 1.5f)));
        c[2] = vec3_add(p, vec3_add(vec3_scale(right, 1.0f), vec3_scale(up, -0.5f)));
        c[3] = vec3_add(p, vec3_add(vec3_scale(right, -1.0f), vec3_scale(up, -0.5f)));
        add_quad(&quads[i * 6], c);
        for (k = 0; k < 6; k++) {
            quads[i * 6 + k].uv[0] = uv[order[k]][0];
            quads[i * 6 + k].uv[1] = uv[order[k]][1];
        }
    }
    make_overlay(&v.billboards, quads, n * 6, v.billboard_texture, 0);
    free(quads);
}

/* A square of half size r lying on the floor under p, lifted 0.01 so it never loses the depth test. */
static void add_floor_square(XVertex *out, Vec3 p, float r)
{
    Vec3 c[4];

    c[0] = vec3(p.x - r, -0.99f, p.z + r);
    c[1] = vec3(p.x + r, -0.99f, p.z + r);
    c[2] = vec3(p.x + r, -0.99f, p.z - r);
    c[3] = vec3(p.x - r, -0.99f, p.z - r);
    add_quad(out, c);
}

static void set_color(float *rgba, uint32_t argb)
{
    rgba[0] = (float)((argb >> 16) & 0xff) / 255.0f;
    rgba[1] = (float)((argb >> 8) & 0xff) / 255.0f;
    rgba[2] = (float)(argb & 0xff) / 255.0f;
    rgba[3] = (float)(argb >> 24) / 255.0f;
}

/*
 * Each ball's shadow is bshadow.png on a square lying on the floor under
 * it, half size 1.5, or 2 for a powerball (0x40ec70, 0x401c60); a prize's
 * is half size 1, shrinking with it. Infravision puts infravis.png, half
 * size 0.25, facing the camera at the ball's centre and drawn over
 * everything (0x40f0d0).
 */
static void build_ball_overlays(Mat4 view)
{
    XVertex shadows[(PLAY_MAX_BALLS + PLAY_MAX_PRIZES) * 6], glows[PLAY_MAX_BALLS * 6];
    int i, n = 0, g = 0;

    for (i = 0; i < v.play.ball_count; i++) {
        const Ball *ball = &v.play.balls[i];
        add_floor_square(&shadows[n], ball->position, (ball->flags & BALL_POWER) ? 2.0f : 1.5f);
        add_billboard(&glows[g], view, ball->position, 0.25f, 0.25f);
        n += 6;
        g += 6;
    }
    for (i = 0; i < v.play.prize_count; i++) {
        add_floor_square(&shadows[n], v.play.prizes[i].position, v.play.prizes[i].glow);
        n += 6;
    }
    make_overlay(&v.shadows, shadows, n, v.shadow_texture, 0);
    make_overlay(&v.glows, glows, v.options.infravision ? g : 0, v.glow_texture, RENDER_NO_DEPTH_TEST);
}

/*
 * The glow round each prize: infravis.png tinted with the prize's colour,
 * its size and offset from the prize's file, depth tested but not
 * written (0x41f8d0's caller).
 */
static void build_prize_glows(Mat4 view)
{
    XVertex quads[PLAY_MAX_PRIZES * 6];
    RenderPart part;
    int i;

    render_mesh_destroy(&v.prize_glows);
    for (i = 0; i < v.play.prize_count; i++) {
        const Prize *prize = &v.play.prizes[i];
        const PrizeInfo *info = &v.play.prize_info[prize->kind];
        Vec3 p = vec3_add(prize->position,
                          vec3(info->glow_offset[0], info->glow_offset[1], info->glow_offset[2]));
        add_billboard(&quads[i * 6], view, p, info->glow_size[0] * prize->glow,
                      info->glow_size[1] * prize->glow);
    }
    if (v.play.prize_count == 0 || !render_mesh_create(&v.prize_glows, quads, v.play.prize_count * 6))
        return;
    for (i = 0; i < v.play.prize_count; i++) {
        memset(&part, 0, sizeof(part));
        part.first_vertex = i * 6;
        part.vertex_count = 6;
        part.texture = v.glow_texture;
        set_color(part.diffuse, v.play.prize_info[v.play.prizes[i].kind].color);
        part.flags = RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_TINT | RENDER_TWO_SIDED;
        part.pose = -1;
        render_mesh_add_part(&v.prize_glows, &part);
    }
}

/* A mesh of quads, each its own part tinted its own colour. */
static void make_tinted_quads(RenderMesh *mesh, const XVertex *quads, const uint32_t *colors, int count,
                              unsigned texture, uint32_t flags)
{
    RenderPart part;
    int i;

    render_mesh_destroy(mesh);
    if (count == 0 || !render_mesh_create(mesh, quads, count * 6))
        return;
    for (i = 0; i < count; i++) {
        memset(&part, 0, sizeof(part));
        part.first_vertex = i * 6;
        part.vertex_count = 6;
        part.texture = texture;
        set_color(part.diffuse, colors[i]);
        part.flags = flags | RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_TINT | RENDER_TWO_SIDED;
        part.pose = -1;
        render_mesh_add_part(mesh, &part);
    }
}

/*
 * Shots' shadows (0x40ec70): bshadow.png on the floor, half size x and z
 * from the bullet file's shadowsize, in its shadowcolor.
 */
static void build_bullet_shadows(void)
{
    XVertex quads[PLAY_MAX_BULLETS * 6];
    uint32_t colors[PLAY_MAX_BULLETS];
    int i;

    for (i = 0; i < v.play.bullet_count; i++) {
        const Bullet *bullet = &v.play.bullets[i];
        const BulletInfo *info = &v.play.bullet_info[bullet->weapon];
        Vec3 p = bullet->position, c[4];
        float sx = info->shadow_size[0], sz = info->shadow_size[2];

        c[0] = vec3(p.x - sx, -0.99f, p.z + sz);
        c[1] = vec3(p.x + sx, -0.99f, p.z + sz);
        c[2] = vec3(p.x + sx, -0.99f, p.z - sz);
        c[3] = vec3(p.x - sx, -0.99f, p.z - sz);
        add_quad(&quads[i * 6], c);
        colors[i] = info->shadow_color;
    }
    make_tinted_quads(&v.bullet_shadows, quads, colors, v.play.bullet_count, v.shadow_texture, 0);
}

/*
 * The rubber bat's strings (0x40e7f0): a strip of rubber.png from the bat
 * model's origin to each ball on it, thinner the longer it is.
 */
static void build_strings(void)
{
    XVertex quads[PLAY_MAX_BALLS * 6];
    uint32_t colors[PLAY_MAX_BALLS];
    const Bat *bat = &v.play.bat;
    Vec3 anchor = vec3(bat->position.x, bat->position.y - 0.25f, bat->position.z - 1.25f);
    int i, n = 0;

    for (i = 0; i < v.play.ball_count; i++) {
        const Ball *ball = &v.play.balls[i];
        Vec3 p = ball->position, d = vec3_sub(anchor, p), c[4];
        float length = sqrtf(vec3_dot(d, d)), w;

        if (!(ball->flags & BALL_YOYO))
            continue;
        if (length < 10.0f) length = 10.0f;
        if (length > 15.0f) length = 15.0f;
        w = 0.25f / length;
        c[0] = vec3(p.x - w, p.y, p.z);
        c[1] = vec3(p.x + w, p.y, p.z);
        c[2] = vec3(anchor.x + w, anchor.y, anchor.z);
        c[3] = vec3(anchor.x - w, anchor.y, anchor.z);
        add_quad(&quads[n * 6], c);
        colors[n++] = 0xff606060u; /* rubber.png is white; the string shows as #606060 */
    }
    make_tinted_quads(&v.strings, quads, colors, n, v.rubber_texture, 0);
}

/*
 * A powerball's trail: a camera-facing triangle of particle.png per spot,
 * 2.5 across, in the ball's colour, fading out as its 250 ms run out
 * (0x41f4e0). Puffs, smoke and firework sparks are drawn the same way,
 * in their own colour.
 */
static void build_trail(Mat4 view)
{
    static XVertex tris[(PLAY_MAX_TRAIL + PLAY_MAX_PARTICLES + PLAY_MAX_SMOKE + PLAY_MAX_SPARKS) * 3];
    static const float corner[3][4] = {
        { 0.0f, 1.0f, 0.5f, 0.0f }, { 1.0f, -0.5f, 1.0f, 1.0f }, { -1.0f, -0.5f, 0.0f, 1.0f }
    };
    /* Puffs, smoke and sparks follow the trail, in the original's order. */
    const struct { const Particle *list; int count; } puffs[3] = {
        { v.play.particles, v.play.particle_count },
        { v.play.smoke, v.play.smoke_count },
        { v.play.sparks, v.play.spark_count },
    };
    static uint32_t colors[PLAY_MAX_TRAIL + PLAY_MAX_PARTICLES + PLAY_MAX_SMOKE + PLAY_MAX_SPARKS];
    Vec3 right = vec3(view.m[0][0], view.m[1][0], view.m[2][0]);
    Vec3 up = vec3(view.m[0][1], view.m[1][1], view.m[2][1]);
    RenderPart part;
    int i, j, k, n = 0;

    render_mesh_destroy(&v.trail);
    for (j = -1; j < 3; j++) {
        int count = j < 0 ? v.play.trail_count : puffs[j].count;
        for (i = 0; i < count; i++, n++) {
            Vec3 at;
            float size;
            if (j < 0) { /* a trail spot's alpha is its life left in ms */
                const TrailSpot *spot = &v.play.trail[i];
                at = spot->position;
                size = spot->size;
                colors[n] = (spot->color & 0xffffffu) | ((uint32_t)spot->life << 24);
            } else {
                at = puffs[j].list[i].position;
                size = puffs[j].list[i].size;
                colors[n] = puffs[j].list[i].color;
            }
            for (k = 0; k < 3; k++) {
                XVertex *t = &tris[n * 3 + k];
                Vec3 p = vec3_add(at, vec3_add(vec3_scale(right, size * corner[k][0]),
                                               vec3_scale(up, size * corner[k][1])));
                t->pos[0] = p.x;
                t->pos[1] = p.y;
                t->pos[2] = p.z;
                t->normal[0] = 0.0f;
                t->normal[1] = 1.0f;
                t->normal[2] = 0.0f;
                t->uv[0] = corner[k][2];
                t->uv[1] = corner[k][3];
            }
        }
    }
    if (n == 0 || !render_mesh_create(&v.trail, tris, n * 3))
        return;
    for (i = 0; i < n; i++) {
        memset(&part, 0, sizeof(part));
        part.first_vertex = i * 3;
        part.vertex_count = 3;
        part.texture = v.particle_texture;
        set_color(part.diffuse, colors[i]);
        part.flags = RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_TINT | RENDER_TWO_SIDED;
        part.pose = -1;
        render_mesh_add_part(&v.trail, &part);
    }
}

/*
 * Teleporter lightning: the pieces of each ribbon from its tail to its
 * head, lighting.png across it and mirrored piece to piece, unlit and
 * over everything (t_lighting.txt; drawn by 0x40d540).
 */
static void build_bolts(void)
{
    static XVertex tris[PLAY_MAX_BOLTS * (BOLT_POINTS - 1) * 6];
    static const int corner[6] = { 0, 2, 1, 1, 2, 3 };
    RenderPart part;
    int i, k, c, n = 0;

    render_mesh_destroy(&v.bolts);
    for (i = 0; i < v.play.bolt_count; i++) {
        const Bolt *bolt = &v.play.bolts[i];
        int head = (int)(bolt->time / 50), tail = head - (BOLT_POINTS - 1);
        if (tail < 0)
            tail = 0;
        if (head < 1)
            head = 1;
        if (head > BOLT_POINTS - 1)
            head = BOLT_POINTS - 1;
        for (k = tail; k < head; k++) {
            for (c = 0; c < 6; c++) {
                int index = k * 2 + corner[c];
                XVertex *t = &tris[n++];
                t->pos[0] = bolt->vertices[index].x;
                t->pos[1] = bolt->vertices[index].y;
                t->pos[2] = bolt->vertices[index].z;
                t->normal[0] = 0.0f;
                t->normal[1] = 0.0f;
                t->normal[2] = 1.0f;
                t->uv[0] = (index & 1) ? 0.0f : 1.0f;
                t->uv[1] = ((index / 2) & 1) ? 1.0f : 0.0f;
            }
        }
    }
    if (n == 0 || !render_mesh_create(&v.bolts, tris, n))
        return;
    memset(&part, 0, sizeof(part));
    part.first_vertex = 0;
    part.vertex_count = n;
    part.texture = v.bolt_texture;
    set_color(part.diffuse, 0xffffffffu);
    part.flags = RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_TINT | RENDER_TWO_SIDED;
    part.pose = -1;
    render_mesh_add_part(&v.bolts, &part);
}

void play_screen_frame(int width, int height, uint32_t ms)
{
    const Camera *c = &v.camera;
    Mat4 orbit, view, projection, identity, board;
    Vec3 eye;
    float aspect;
    int i;

    /* Fire, fountains and flashing bricks keep their own clock, moved on
       each time they are drawn; the game draws them twice a frame while
       playing and once when paused, so they animate twice as fast in play
       (0x406600, 0x407880). */
    v.object_time += v.playing ? ms * 2 : ms;
    update(ms);
    v.time += ms;

    orbit = mat4_yaw_pitch_roll(c->yaw, c->pitch, c->roll);
    eye = mat4_transform_point(orbit, vec3(0.0f, 0.0f, -c->distance * c->zoom));
    view = mat4_look_at_lh(eye, vec3(0.0f, 0.0f, c->look_z), vec3(0.0f, 1.0f, 0.0f));
    /*
     * The game passes aspect 1.0 for its 4:3 modes, so the picture is
     * stretched to fill the screen. We keep that look at 4:3 and widen
     * the view on wider screens instead of stretching further.
     */
    aspect = ((float)width / (float)(height > 0 ? height : 1)) / (4.0f / 3.0f);
    projection = mat4_perspective_fov_lh(3.14159265f / 4.0f, aspect, 1.0f, 1000.0f);
    identity = mat4_identity();
    /* The board model is stretched by the board file's scalex / scalez, and
       lowered so its floor is where the bricks stand. */
    board = mat4_multiply(mat4_scaling(v.scene.board.scale_x, 1.0f, v.scene.board.scale_z),
                          mat4_translation(0.0f, v.scene.board_y, 0.0f));

    render_begin(width, height, view, projection, &v.light);
    if (!v.scene_loaded)
        return;
    if (v.playing && v.options.smooth)
        blend_positions();
    {
        float t = (float)v.time;
        Mat4 bat_matrix = mat4_identity(), ball_matrices[PLAY_MAX_BALLS];
        Mat4 prize_matrices[PLAY_MAX_PRIZES], *prize_poses[PLAY_MAX_PRIZES];
        const RenderMesh *ball_meshes[PLAY_MAX_BALLS];
        Mat4 *bat_pose = NULL, *ball_pose = pose_model(&v.ball_model, (unsigned)v.time);
        Mat4 *explosive_pose = pose_model(&v.explosive_model, (unsigned)v.time);
        Mat4 *hypnotic_pose = pose_model(&v.hypnotic_model, (unsigned)v.time);
        const Mat4 *ball_poses[PLAY_MAX_BALLS];
        int pass;

        if (v.playing) {
            const Bat *bat = &v.play.bat;
            /* The bat model sits 0.25 down and 1.25 back from the bat's point. */
            bat_matrix = mat4_translation(bat->position.x, bat->position.y - 0.25f,
                                          bat->position.z - 1.25f);
            bool leaning = bat->tilt > 5 || bat->tilt < -5;
            if (leaning) {
                bat_matrix = mat4_multiply(mat4_rotation_z((float)bat->tilt * 0.00025f), bat_matrix);
                /* The turbines lean with the tilt: their keys from 200 lean
                   one way and from 600 the other (0x405178). */
                v.turbine_time = (unsigned)(abs(bat->tilt) + (bat->tilt < 0 ? 200 : 600));
            }
            bat_pose = pose_bat();
            /* Balls are drawn at 0.8 of the model's size, full size when
               powered up, and look different when explosive or hypnotic. */
            for (i = 0; i < v.play.ball_count; i++) {
                const Ball *ball = &v.play.balls[i];
                /* Turned so its spin animation rolls it the way it goes (0x402309).
                   The explosive ball has no animation; it tumbles instead (0x4022eb). */
                float heading = 3.14159265f - atan2f(ball->velocity.z, ball->velocity.x);
                float size = (ball->flags & BALL_POWER) ? 1.0f : 0.8f;
                Mat4 turn = (ball->flags & BALL_EXPLOSIVE)
                                ? mat4_yaw_pitch_roll(1.0472f, ball->spin / 3.0f, ball->spin)
                                : mat4_rotation_y(heading);
                ball_matrices[i] = mat4_multiply(
                    mat4_multiply(mat4_scaling(size, size, size), turn),
                    mat4_translation(ball->position.x, ball->position.y, ball->position.z));
                ball_meshes[i] = &v.ball;
                ball_poses[i] = ball_pose;
                if (ball->flags & BALL_EXPLOSIVE) {
                    ball_meshes[i] = &v.explosive_ball;
                    ball_poses[i] = explosive_pose;
                } else if (ball->flags & BALL_HYPNOTIC) {
                    ball_meshes[i] = &v.hypnotic_ball;
                    ball_poses[i] = hypnotic_pose;
                }
            }
            /* Prizes play their animation from when they appeared (0x41f8d0). */
            for (i = 0; i < v.play.prize_count; i++) {
                const Prize *prize = &v.play.prizes[i];
                prize_matrices[i] = mat4_multiply(
                    mat4_scaling(prize->scale, prize->scale, prize->scale),
                    mat4_translation(prize->position.x, prize->position.y, prize->position.z));
                prize_poses[i] = pose_model(&v.prize_models[prize->kind], prize->age);
            }
        }
        for (pass = 0; pass < 2; pass++) {
            RenderPass kind = pass == 0 ? RENDER_PASS_SOLID : RENDER_PASS_BLENDED;
            render_draw(&v.background, identity, kind, t);
            render_draw(&v.board, board, kind, t);
            /*
             * The original draws the bricks texture by texture, in the
             * texture set's order, blended or not. Glass (tileset 4, no depth
             * writes) so goes under bricks behind it drawn from a later
             * tileset; copy that rather than sorting.
             */
            if (pass == 0) {
                render_draw(&v.bricks, identity, RENDER_PASS_ALL, t);
                build_billboards(view);
                render_draw(&v.billboards, identity, RENDER_PASS_ALL, t);
                if (v.playing) {
                    build_ball_overlays(view);
                    render_draw(&v.shadows, identity, RENDER_PASS_BLENDED, t);
                    build_bullet_shadows();
                    render_draw(&v.bullet_shadows, identity, RENDER_PASS_BLENDED, t);
                }
            }
            draw_animals(kind, t);
            if (v.playing) {
                if (v.play.bat.mode != BAT_DEAD && v.play.state != PLAY_GAME_OVER)
                    render_draw_posed(&v.bat, bat_matrix, bat_pose, kind, t);
                else if (pass == 1 && v.play.bat.pieces_ms > 0)
                    draw_bat_pieces(bat_matrix, bat_pose, t);
                /* An explosive ball from the fire fades out over its last
                   second, to 55 of 255. */
                for (i = 0; i < v.play.ball_count; i++) {
                    const Ball *ball = &v.play.balls[i];
                    if ((ball->flags & BALL_EXPLOSIVE) && ball->explosive_end != PLAY_FOREVER &&
                        ball->explosive_end - ball->age <= 1000)
                        render_set_fade(((float)(ball->explosive_end - ball->age) / 5.0f + 55.0f) / 255.0f);
                    render_draw_posed(ball_meshes[i], ball_matrices[i], ball_poses[i], kind, t);
                    render_set_fade(1.0f);
                }
                /* Shots spin about their length and point along their
                   climb or fall (0x4092e0). */
                for (i = 0; i < v.play.bullet_count; i++) {
                    const Bullet *b = &v.play.bullets[i];
                    float ahead = sqrtf(b->velocity.x * b->velocity.x + b->velocity.z * b->velocity.z);
                    Mat4 world = mat4_multiply(
                        mat4_multiply(mat4_rotation_z(b->spin), mat4_rotation_x(-atan2f(b->velocity.y, ahead))),
                        mat4_translation(b->position.x, b->position.y, b->position.z));
                    render_draw(&v.bullet_meshes[b->weapon], world, kind, t);
                }
                /* A firework rocket just goes straight up (0x40bf97). */
                for (i = 0; i < v.play.firework_count; i++) {
                    Vec3 p = v.play.fireworks[i].position;
                    render_draw(&v.firework_mesh, mat4_translation(p.x, p.y, p.z), kind, t);
                }
                /* A prize's glow goes first, without depth writes, so the
                   model covers it. */
                if (pass == 0) {
                    build_prize_glows(view);
                    render_draw(&v.prize_glows, identity, RENDER_PASS_ALL, t);
                }
                for (i = 0; i < v.play.prize_count; i++)
                    render_draw_posed(&v.prize_meshes[v.play.prizes[i].kind], prize_matrices[i],
                                      prize_poses[i], kind, t);
            }
        }
        free(bat_pose);
        free(ball_pose);
        free(explosive_pose);
        free(hypnotic_pose);
        if (v.playing) {
            for (i = 0; i < v.play.prize_count; i++)
                free(prize_poses[i]);
            build_strings();
            render_draw(&v.strings, identity, RENDER_PASS_BLENDED, t);
            build_trail(view);
            render_draw(&v.trail, identity, RENDER_PASS_BLENDED, t);
            build_bolts();
            render_draw(&v.bolts, identity, RENDER_PASS_BLENDED, t);
        }
        build_explosion_mesh(view);
        render_draw(&v.explosions, identity, RENDER_PASS_BLENDED, t);
        if (v.playing)
            render_draw(&v.glows, identity, RENDER_PASS_BLENDED, t);
    }
    if (v.playing && v.options.smooth)
        unblend_positions();
}
