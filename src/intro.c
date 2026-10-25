/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "intro.h"
#include "assets.h"
#include "core/config.h"
#include "core/log.h"
#include "core/matrix.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* It ends here, or on a key or a click (0x41af00). */
#define INTRO_LENGTH 8000.0f
#define MAX_PARTICLES 4096
#define PI 3.14159265f

/*
 * The camera's flight: from each key to the next along a Hermite curve
 * through the positions, with the tangents given (0x409b30). Two keys at
 * one time make a sharp turn.
 */
typedef struct CameraKey {
    float time;
    Vec3 position, tangent;
} CameraKey;

static const CameraKey k_camera[] = {
    { 0.0f, { 0.0f, 12.0f, 3.0f }, { 0.0f, 1.0f, 0.0f } },
    { 3000.0f, { 0.0f, 12.0f, 3.0f }, { 0.0f, 0.0f, 0.0f } },
    { 3000.0f, { 0.0f, 12.0f, 3.0f }, { -10.0f, 10.0f, -40.0f } },
    { 4000.0f, { -20.0f, 16.0f, 3.0f }, { 0.0f, 0.0f, 30.0f } },
    { 5500.0f, { 0.0f, 4.0f, 30.0f }, { 50.0f, -20.0f, 0.0f } },
    { 5500.0f, { 0.0f, 4.0f, 30.0f }, { 0.0f, 0.0f, 0.0f } },
    { 25000.0f, { 0.0f, 4.0f, 30.0f }, { 0.0f, 0.0f, 0.0f } },
};

typedef struct Particle {
    Vec3 position, velocity;
    float size;
} Particle;

static struct {
    bool started;
    XModel logo_model, bat_model;
    RenderMesh logo, bat;
    RenderPart *scratch; /* the logo's parts picked for one draw */
    float time;
    float plate_alpha, ball_alpha, plane_alpha;
    float bat_z;
    bool kicked;          /* the bat has hit the ball */
    unsigned bounce_ms;   /* since then */
    Mat4 view;
    Particle *particles;
    int particle_count;
    XVertex *particle_vertices;
    unsigned particle_texture;
} in;

static bool bat_part(const XPart *part)
{
    return xpart_in_frame(part, "Corpus") || xpart_in_frame(part, "L_Hand") ||
           xpart_in_frame(part, "R_Hand") || xpart_in_frame(part, "Plashka");
}

bool intro_start(void)
{
    char path[128];
    Config cfg;

    intro_stop();
    if (!xfile_load(&in.logo_model, "SkinMeshes/LOGO.X")) {
        log_warn("intro: can't load SkinMeshes/LOGO.X");
        return false;
    }
    asset_model_mesh(&in.logo, &in.logo_model, 0, "", "", NULL);
    in.scratch = malloc((size_t)(in.logo.part_count + 1) * sizeof(*in.scratch));
    in.particles = malloc(MAX_PARTICLES * sizeof(*in.particles));
    in.particle_vertices = malloc(MAX_PARTICLES * 3 * sizeof(*in.particle_vertices));

    /* The bat on the floor is the default one, whatever was picked. */
    snprintf(path, sizeof(path), "SkinMeshes/pad.x");
    if (config_load(&cfg, "Pads/common.txt")) {
        snprintf(path, sizeof(path), "SkinMeshes/%s", config_get_string(&cfg, "grdata", "pad.x"));
        config_free(&cfg);
    }
    if (xfile_load(&in.bat_model, path))
        asset_model_mesh(&in.bat, &in.bat_model, 0, "", "", bat_part);
    else
        log_warn("intro: can't load the bat model %s", path);

    in.particle_texture = asset_texture("particle.png");
    in.bat_z = -6.0f;
    in.started = in.scratch != NULL && in.particles != NULL && in.particle_vertices != NULL;
    return in.started;
}

void intro_stop(void)
{
    render_mesh_destroy(&in.logo);
    render_mesh_destroy(&in.bat);
    xfile_free(&in.logo_model);
    xfile_free(&in.bat_model);
    free(in.scratch);
    free(in.particles);
    free(in.particle_vertices);
    memset(&in, 0, sizeof(in));
}

static Vec3 hermite(Vec3 p1, Vec3 t1, Vec3 p2, Vec3 t2, float s)
{
    float s2 = s * s, s3 = s2 * s;
    return vec3_add(vec3_add(vec3_scale(p1, 2.0f * s3 - 3.0f * s2 + 1.0f), vec3_scale(t1, s3 - 2.0f * s2 + s)),
                    vec3_add(vec3_scale(p2, -2.0f * s3 + 3.0f * s2), vec3_scale(t2, s3 - s2)));
}

/* Past the last key the camera stays where it was. */
static void update_camera(void)
{
    int count = (int)(sizeof(k_camera) / sizeof(k_camera[0])), i;

    for (i = 1; i < count; i++) {
        if (in.time < k_camera[i].time) {
            const CameraKey *a = &k_camera[i - 1], *b = &k_camera[i];
            float s = (in.time - a->time) / (b->time - a->time);
            Vec3 eye = hermite(a->position, a->tangent, b->position, b->tangent, s);
            in.view = mat4_look_at_lh(eye, vec3(0.0f, 0.0f, 3.5f), vec3(0.0f, 1.0f, 0.1f));
            return;
        }
    }
}

/* min plus a random part of range, in steps of 1/10000 (0x40d1c0). */
static float random_in(float min, float range)
{
    return min + (float)(rand() % 10000) * 0.0001f * range;
}

static void emit_particles(int count)
{
    while (count-- > 0 && in.particle_count < MAX_PARTICLES) {
        Particle *p = &in.particles[in.particle_count++];
        p->position = vec3(0.5f, 1.5f, 1.0f);
        p->velocity = vec3(random_in(-0.8f, 1.6f), random_in(-0.5f, 1.0f), random_in(1.5f, 1.5f));
        p->size = random_in(0.5f, 0.75f);
    }
}

/* Sparks fly, fall, shrink and bounce off the floor (0x40d400). */
static void update_particles(float f)
{
    int i = 0;

    while (i < in.particle_count) {
        Particle *p = &in.particles[i];
        p->position = vec3_add(p->position, vec3_scale(p->velocity, f));
        p->velocity.y -= f * 0.02f;
        p->size -= f * 0.01f;
        if (p->size < 0.25f) {
            *p = in.particles[--in.particle_count];
            continue;
        }
        if (p->position.y < -0.75f)
            p->velocity.y = fabsf(p->velocity.y) * 0.75f;
        i++;
    }
}

/* A yellow triangle facing the camera for each spark, without lighting. */
static void draw_particles(void)
{
    Vec3 right = vec3(in.view.m[0][0], in.view.m[1][0], in.view.m[2][0]);
    Vec3 up = vec3(in.view.m[0][1], in.view.m[1][1], in.view.m[2][1]);
    static const float corners[3][4] = { { 0.0f, 1.0f, 0.5f, 0.0f },
                                         { 1.0f, -0.5f, 1.0f, 1.0f },
                                         { -1.0f, -0.5f, 0.0f, 1.0f } };
    RenderMesh mesh;
    RenderPart part;
    int i, c;

    if (in.particle_count == 0)
        return;
    for (i = 0; i < in.particle_count; i++) {
        const Particle *p = &in.particles[i];
        for (c = 0; c < 3; c++) {
            XVertex *v = &in.particle_vertices[i * 3 + c];
            Vec3 at = vec3_add(p->position, vec3_add(vec3_scale(right, corners[c][0] * p->size),
                                                     vec3_scale(up, corners[c][1] * p->size)));
            v->pos[0] = at.x;
            v->pos[1] = at.y;
            v->pos[2] = at.z;
            v->normal[0] = v->normal[1] = 0.0f;
            v->normal[2] = -1.0f;
            v->uv[0] = corners[c][2];
            v->uv[1] = corners[c][3];
        }
    }
    if (!render_mesh_create(&mesh, in.particle_vertices, in.particle_count * 3))
        return;
    memset(&part, 0, sizeof(part));
    part.vertex_count = in.particle_count * 3;
    part.texture = in.particle_texture;
    part.diffuse[0] = part.diffuse[1] = part.diffuse[3] = 1.0f; /* 0xffffff00 */
    part.flags = RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_TWO_SIDED | RENDER_TINT;
    part.pose = -1;
    render_mesh_add_part(&mesh, &part);
    render_draw(&mesh, mat4_identity(), RENDER_PASS_ALL, 0.0f);
    render_mesh_destroy(&mesh);
}

static bool part_named(const XPart *part, const char *name)
{
    size_t length = strlen(name);
    /* "pCone" stands for the hundred cones pCone0 to pCone99. */
    if (strcmp(name, "pCone") == 0)
        return strncmp(part->frame, name, length) == 0;
    return xpart_in_frame(part, name);
}

/*
 * Draw the logo's frames with the name given, blended. alpha < 0 keeps
 * the material's own; the game sets it on the frame's materials
 * (0x4219b0). Without lighting Direct3D takes white from the vertices,
 * so then only the texture's alpha counts.
 */
static void draw_logo_frame(const char *name, float alpha, bool lit, const Mat4 *pose, Mat4 world)
{
    RenderMesh subset = in.logo;
    int p;

    subset.parts = in.scratch;
    subset.part_count = 0;
    for (p = 0; p < in.logo.part_count; p++) {
        const RenderPart *part = &in.logo.parts[p];
        RenderPart *copy;
        if (!part_named(&in.logo_model.parts[part->pose], name))
            continue;
        copy = &in.scratch[subset.part_count++];
        *copy = *part;
        copy->flags = RENDER_ALPHA | (lit ? 0u : (uint32_t)RENDER_NOT_LIGHTED);
        if (alpha >= 0.0f)
            copy->diffuse[3] = alpha;
    }
    render_draw_posed(&subset, world, pose, RENDER_PASS_ALL, 0.0f);
}

/*
 * The bat's own update runs as in a game (0x404f4f): the fans spin, and
 * the plate, hands and springs take the bounce's time, 0 when there is
 * none. At 5 seconds the bat kicks the ball into the logo with the bounce
 * a hit in the middle gives (0x41b14a): keys 1-150 at a third of real
 * time.
 */
static void draw_bat(uint32_t ms)
{
    static const char *const bounce_frames[] = { "Plashka", "L_Hand",    "R_Hand",
                                                 "L_Ressore", "R_Ressore", "Magnit" };
    const XModel *model = &in.bat_model;
    unsigned *times, bounce = 0;
    Mat4 *pose;
    int f, k;

    if (in.bat.buffer == 0)
        return;
    /* It slides in towards the logo and stops (0x403e80). */
    if (in.bat_z < -1.75f)
        in.bat_z += (float)ms * 0.0045f;
    if (in.bat_z > -1.75f)
        in.bat_z = -1.75f;

    if (in.time > 5000.0f && !in.kicked) {
        in.kicked = true;
        in.bounce_ms = 0;
    } else if (in.kicked) {
        in.bounce_ms += ms;
    }
    if (in.kicked && 1 + in.bounce_ms / 3 <= 150)
        bounce = 1 + in.bounce_ms / 3;

    times = malloc((size_t)(model->frame_count + 1) * sizeof(*times));
    pose = malloc((size_t)(model->part_count + 1) * sizeof(*pose));
    if (times != NULL && pose != NULL) {
        for (f = 0; f < model->frame_count; f++) {
            const char *name = model->frames[f].name;
            times[f] = XFRAME_REST;
            if (strcmp(name, "L_Fan") == 0 || strcmp(name, "R_Fan") == 0)
                times[f] = (unsigned)in.time;
            for (k = 0; k < 6; k++)
                if (strcmp(name, bounce_frames[k]) == 0)
                    times[f] = bounce;
        }
        xmodel_pose(model, times, pose);
        render_draw_posed(&in.bat,
                          mat4_multiply(mat4_scaling(2.0f, 2.0f, 2.0f), mat4_translation(0.5f, 1.0f, in.bat_z)),
                          pose, RENDER_PASS_SOLID, 0.0f);
        render_draw_posed(&in.bat,
                          mat4_multiply(mat4_scaling(2.0f, 2.0f, 2.0f), mat4_translation(0.5f, 1.0f, in.bat_z)),
                          pose, RENDER_PASS_BLENDED, 0.0f);
    }
    free(times);
    free(pose);
}

/* The logo (0x40cd20): what fades in when, and the order it is drawn in. */
static void draw_logo(uint32_t ms)
{
    static const float k_fog[3] = { 0.0f, 0.0f, 170.0f / 255.0f }; /* 0x0000aa */
    Mat4 world = mat4_rotation_y(PI);
    float step = (float)ms;
    float cones;
    Mat4 *pose;

    if (in.time < 1500.0f) {
        in.plate_alpha = fminf(in.plate_alpha + step * 0.0005f, 1.0f);
        in.ball_alpha = 0.0f;
    } else if (in.time < 3000.0f) {
        in.plate_alpha = fmaxf(in.plate_alpha - step * 0.0005f, 0.0f);
        in.ball_alpha = 1.0f - in.plate_alpha;
    }
    if (in.time > 2500.0f)
        in.plane_alpha = fminf(in.plane_alpha + step * 0.00085f, 1.0f);
    else
        in.plane_alpha = 0.0f;

    pose = asset_pose(&in.logo_model, (unsigned)(in.time * 3.0f));
    if (pose == NULL)
        return;

    render_set_fog(true, 20.0f, 75.0f, k_fog);
    draw_logo_frame("bnd", -1.0f, true, pose, world);
    draw_logo_frame("Plane01", in.plane_alpha, true, pose, world);
    draw_logo_frame("Plane02", in.plane_alpha, true, pose, world);
    draw_logo_frame("logoPlate", in.plate_alpha, true, pose, world);
    draw_particles();
    if (in.time > 1500.0f)
        draw_logo_frame("ball", in.ball_alpha, true, pose, world);
    render_set_fog(false, 0.0f, 1.0f, NULL);

    /* The burst of sparks as the title comes out. */
    if (in.time > 5400.0f && in.time < 5700.0f)
        emit_particles(32);

    cones = in.time > 5700.0f ? fminf((in.time - 5000.0f) * 0.001f, 1.0f) : 0.0f;
    draw_logo_frame("pCone", cones, true, pose, world);
    draw_logo_frame("cub2", -1.0f, false, pose, world);
    draw_logo_frame("MagicLogo", -1.0f, false, pose, world);
    free(pose);
}

bool intro_frame(int width, int height, const int viewport[4], uint32_t ms, const RenderLight *light)
{
    RenderLight lit = *light;
    Mat4 projection;
    int i;

    if (!in.started)
        return false;
    in.time += (float)ms;
    if (in.time > INTRO_LENGTH)
        return false;

    update_camera();
    update_particles((float)ms * 0.01f);

    /* The original's 4:3 picture squeezed into a square projection, wider
       on a wider screen. */
    projection = mat4_perspective_fov_lh(PI / 4.0f,
                                         (float)viewport[2] * 3.0f / (float)(4 * (viewport[3] > 0 ? viewport[3] : 1)),
                                         1.0f, 1000.0f);
    /* The scene's ambient adds to the light's (0x41a8c3). */
    for (i = 0; i < 3; i++)
        lit.ambient[i] = fminf(lit.ambient[i] + 34.0f / 255.0f, 1.0f);
    render_begin(width, height, in.view, projection, &lit);
    render_viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (in.time > 2000.0f)
        draw_bat(ms);
    draw_logo(ms);
    return true;
}
