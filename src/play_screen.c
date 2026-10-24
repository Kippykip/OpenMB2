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
    XModel bat_model, ball_model;
    RenderMesh background, board, bricks, bat, ball;
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
            /* Guess: movex/movey are UV units per frame at the original's ~60 fps. */
            part.uv_speed[0] = script->movex / 16.67f;
            part.uv_speed[1] = script->movey / 16.67f;
        } else {
            log_warn("brick texture index %d is not in the texture set", s->batches[i].texture);
            part.texture = render_texture(NULL);
        }
        render_mesh_add_part(&v.bricks, &part);
        at += part.vertex_count;
    }
}

/* The background is seen from inside, and isn't lit (a guess, to check). */
static void build_background_mesh(void)
{
    render_mesh_destroy(&v.background);
    build_model_mesh(&v.background, &v.scene.background_model,
                     RENDER_TWO_SIDED | RENDER_NOT_LIGHTED, v.scene.background_texture, "", NULL);
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

/* The bat model named by the level's pad file ("grdata"). */
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
    if (xfile_load(&v.bat_model, path))
        build_model_mesh(&v.bat, &v.bat_model, 0, "", "", plain_bat_part);
    else
        log_warn("can't load the bat model %s", path);
}

static void unload_level(void)
{
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
    if (xfile_load(&v.ball_model, "SkinMeshes/s_ball.x"))
        build_model_mesh(&v.ball, &v.ball_model, 0, "", "", NULL);
    else
        log_warn("can't load the ball model SkinMeshes/s_ball.x");
    v.explosion_texture = texture_for_name("explose.png");
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
    xfile_free(&v.bat_model);
    xfile_free(&v.ball_model);
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
            else
                play_click(&v.play);
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_RIGHT)
            v.dragging = false;
        break;
    case SDL_MOUSEMOTION:
        if (v.dragging) {
            /* The original turns the camera by 0.005 per mouse step. */
            v.camera.yaw += (float)event->motion.xrel * 0.005f;
            v.camera.pitch += (float)event->motion.yrel * 0.005f;
            if (v.camera.pitch < 0.05f) v.camera.pitch = 0.05f;
            if (v.camera.pitch > 1.55f) v.camera.pitch = 1.55f;
        } else if (v.playing) {
            play_mouse_move(&v.play, event->motion.xrel);
        }
        break;
    case SDL_MOUSEWHEEL:
        v.camera.zoom *= event->wheel.y > 0 ? 0.9f : 1.0f / 0.9f;
        break;
    default:
        break;
    }
}

static void update(uint32_t ms)
{
    if (!v.playing)
        return;
    if (v.options.autoplay)
        play_autopilot(&v.play);
    play_update(&v.play, ms);
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

void play_screen_frame(int width, int height, uint32_t ms)
{
    const Camera *c = &v.camera;
    Mat4 orbit, view, projection, identity, board;
    Vec3 eye;
    float aspect;
    int i;

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
    {
        float t = (float)v.time;
        Mat4 bat_matrix = mat4_identity(), ball_matrices[PLAY_MAX_BALLS];
        int pass;

        if (v.playing) {
            const Bat *bat = &v.play.bat;
            /* The bat model sits 0.25 down and 1.25 back from the bat's point. */
            bat_matrix = mat4_translation(bat->position.x, bat->position.y - 0.25f,
                                          bat->position.z - 1.25f);
            if (bat->tilt > 5 || bat->tilt < -5)
                bat_matrix = mat4_multiply(mat4_rotation_z((float)bat->tilt * 0.00025f), bat_matrix);
            /* Balls are drawn at 0.8 of the model's size. */
            for (i = 0; i < v.play.ball_count; i++) {
                const Ball *ball = &v.play.balls[i];
                ball_matrices[i] = mat4_multiply(mat4_scaling(0.8f, 0.8f, 0.8f),
                                                 mat4_translation(ball->position.x, ball->position.y,
                                                                  ball->position.z));
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
            if (pass == 0)
                render_draw(&v.bricks, identity, RENDER_PASS_ALL, t);
            if (v.playing) {
                if (v.play.state != PLAY_BAT_LOST && v.play.state != PLAY_GAME_OVER)
                    render_draw(&v.bat, bat_matrix, kind, t);
                for (i = 0; i < v.play.ball_count; i++)
                    render_draw(&v.ball, ball_matrices[i], kind, t);
            }
        }
        build_explosion_mesh(view);
        render_draw(&v.explosions, identity, RENDER_PASS_BLENDED, t);
    }
}
