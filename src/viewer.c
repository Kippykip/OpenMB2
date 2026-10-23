/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "viewer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/vfs.h"
#include "game/scene.h"
#include "render/render.h"

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

static struct {
    SDL_Window *window;
    Pack packs[PACK_FOLDER_COUNT];
    int pack_count;
    int pack, level; /* what's showing */
    Scene scene;
    bool scene_loaded;
    RenderMesh background, board, bricks;
    RenderLight light;
    Camera camera;
    bool dragging;
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
 * texture of every part in override_frame ("" = all parts).
 */
static void build_model_mesh(RenderMesh *mesh, const XModel *model, uint32_t flags,
                             const char *override, const char *override_frame)
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
                     RENDER_TWO_SIDED | RENDER_NOT_LIGHTED, v.scene.background_texture, "");
}

static void build_board_mesh(void)
{
    render_mesh_destroy(&v.board);
    build_model_mesh(&v.board, &v.scene.board_model, 0, v.scene.board_texture, "Doska");
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

static void unload_level(void)
{
    render_mesh_destroy(&v.background);
    render_mesh_destroy(&v.board);
    render_mesh_destroy(&v.bricks);
    if (v.scene_loaded)
        scene_free(&v.scene);
    v.scene_loaded = false;
}

static void load_level(int pack, int level)
{
    const Pack *p;
    char path[256], title[320];
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

    ts = &v.scene.textures;
    memcpy(v.light.direction, ts->light_dir, sizeof(v.light.direction));
    memcpy(v.light.ambient, ts->ambient, sizeof(v.light.ambient));
    memcpy(v.light.diffuse, ts->diffuse, sizeof(v.light.diffuse));
    reset_camera();

    log_info("level %s: %d bricks, %d other objects, board %s", path, v.scene.brick_count,
             v.scene.other_count, v.scene.level.slots[LEVEL_SLOT_BOARD]);
    snprintf(title, sizeof(title), "OpenMB2 - %s (%d/%d)", path, v.level + 1, p->level_count);
    SDL_SetWindowTitle(v.window, title);
}

/* ------------------------------------------------------------------------ */

bool viewer_init(SDL_Window *window, const char *start_level)
{
    int pack = 0, level = 0, i, j;

    memset(&v, 0, sizeof(v));
    v.window = window;
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
    load_level(pack, level);
    return true;
}

void viewer_shutdown(void)
{
    int i;
    unload_level();
    for (i = 0; i < v.pack_count; i++)
        free(v.packs[i].levels);
    render_shutdown();
    memset(&v, 0, sizeof(v));
}

void viewer_handle_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_KEYDOWN:
        switch (event->key.keysym.sym) {
        case SDLK_RIGHT: load_level(v.pack, v.level + 1); break;
        case SDLK_LEFT: load_level(v.pack, v.level - 1); break;
        case SDLK_PAGEDOWN: load_level(v.pack + 1, 0); break;
        case SDLK_PAGEUP: load_level(v.pack - 1, 0); break;
        case SDLK_SPACE: reset_camera(); break;
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
        if (event->button.button == SDL_BUTTON_LEFT)
            v.dragging = true;
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT)
            v.dragging = false;
        break;
    case SDL_MOUSEMOTION:
        if (v.dragging) {
            v.camera.yaw += (float)event->motion.xrel * 0.01f;
            v.camera.pitch += (float)event->motion.yrel * 0.01f;
            if (v.camera.pitch < 0.05f) v.camera.pitch = 0.05f;
            if (v.camera.pitch > 1.55f) v.camera.pitch = 1.55f;
        }
        break;
    case SDL_MOUSEWHEEL:
        v.camera.zoom *= event->wheel.y > 0 ? 0.9f : 1.0f / 0.9f;
        break;
    default:
        break;
    }
}

void viewer_draw(int width, int height, float time_ms)
{
    const Camera *c = &v.camera;
    Mat4 orbit = mat4_yaw_pitch_roll(c->yaw, c->pitch, c->roll);
    Vec3 eye = mat4_transform_point(orbit, vec3(0.0f, 0.0f, -c->distance * c->zoom));
    Mat4 view = mat4_look_at_lh(eye, vec3(0.0f, 0.0f, c->look_z), vec3(0.0f, 1.0f, 0.0f));
    /*
     * The game passes aspect 1.0 for its 4:3 modes, so the picture is
     * stretched to fill the screen. We keep that look at 4:3 and widen
     * the view on wider screens instead of stretching further.
     */
    float aspect = ((float)width / (float)(height > 0 ? height : 1)) / (4.0f / 3.0f);
    Mat4 projection = mat4_perspective_fov_lh(3.14159265f / 4.0f, aspect, 1.0f, 1000.0f);
    Mat4 identity = mat4_identity();
    /* The board model is stretched by the board file's scalex / scalez, and
       lowered so its floor is where the bricks stand. */
    Mat4 board = mat4_multiply(mat4_scaling(v.scene.board.scale_x, 1.0f, v.scene.board.scale_z),
                               mat4_translation(0.0f, v.scene.board_y, 0.0f));

    render_begin(width, height, view, projection, &v.light);
    if (!v.scene_loaded)
        return;
    render_draw(&v.background, identity, false, time_ms);
    render_draw(&v.board, board, false, time_ms);
    render_draw(&v.bricks, identity, false, time_ms);
    render_draw(&v.background, identity, true, time_ms);
    render_draw(&v.board, board, true, time_ms);
    render_draw(&v.bricks, identity, true, time_ms);
}
