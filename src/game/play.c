/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "play.h"
#include "../core/config.h"
#include "../core/log.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP_MS         18      /* the original runs a frame once > 17 ms have passed */
#define BAT_HALF_WIDTH  3.0f
#define BALL_Y          -0.5f
#define BAT_REACH       1.3f    /* how far in front of the bat line it can hit */
#define BAT_ANGLE       1.0472f /* 60 degrees, at the very end of the bat */
#define STUCK_Z         1.35f   /* a stuck ball sits this far in front of the bat */
#define MIN_SPEED_Z     0.15f
#define BAT_LOST_MS     2000    /* guess: the pause before a new ball */
#define BAT_MIN_WIDTH   2.0f    /* the half widths prizes can make the bat */
#define BAT_MAX_WIDTH   4.5f
#define GRID_X 34
#define GRID_Y 15
#define GRID_Z 18

typedef struct Cell {
    int x, y, z;
} Cell;

/* The original's grid: cells are 1 wide, 2 high and 2 deep (0x40dce0). */
static Cell cell_of(Vec3 p)
{
    Cell c;
    c.x = (int)floorf(p.x) + 17;
    c.y = (int)((p.y + 0.9f) * 0.5f);
    c.z = (int)floorf(p.z * 0.5f);
    return c;
}

static bool cell_valid(Cell c)
{
    return c.x >= 0 && c.x < GRID_X && c.y >= 0 && c.y < GRID_Y && c.z >= 0 && c.z < GRID_Z;
}

/* Scene brick index at a point, or -1. */
static int brick_at(const Play *play, Vec3 p)
{
    Cell c = cell_of(p);
    if (!cell_valid(c))
        return -1;
    return play->grid[c.x][c.y][c.z] - 1;
}

static void sound(Play *play, const char *name, int volume, int pan)
{
    if (play->sound != NULL)
        play->sound(name, volume, pan, play->sound_user);
}

/* Objects sound quieter further away, and are panned by x (0x403660). */
static void sound_at(Play *play, Vec3 p, const char *name)
{
    int volume = (int)(100.0f - fabsf(p.z + p.z));
    if (volume >= 0)
        sound(play, name, volume, (int)(p.x / play->scene->board.half_width * 100.0f));
}

static void brick_sound(Play *play, int index, const char *name)
{
    sound_at(play, play->scene->bricks[index].position, name);
}

static void wall_sound(Play *play, const Ball *ball)
{
    sound(play, "ball1.ogg", -1, (int)(ball->position.x / play->scene->board.half_width * 100.0f));
}

/* ------------------------------------------------------------------------ */
/* Bricks                                                                   */
/* ------------------------------------------------------------------------ */

static int brick_layers(const SceneBrick *b)
{
    return b->height > 1 ? b->height : 1;
}

static void grid_place(Play *play, int index)
{
    const SceneBrick *b = &play->scene->bricks[index];
    const PlayBrick *pb = &play->bricks[index];
    Cell c = { pb->cell_x, pb->cell_y, pb->cell_z };
    int h, layers = brick_layers(b);

    /* A brick is 2 wide, so it fills two cells in x; tall ones fill more layers. */
    for (h = 0; h < layers; h++) {
        Cell at = c;
        at.y += h;
        if (!cell_valid(at) || at.x + 1 >= GRID_X)
            continue;
        if (play->grid[at.x][at.y][at.z] == 0)
            play->grid[at.x][at.y][at.z] = (int16_t)(index + 1);
        if (play->grid[at.x + 1][at.y][at.z] == 0)
            play->grid[at.x + 1][at.y][at.z] = (int16_t)(index + 1);
    }
}

static void grid_remove(Play *play, int index)
{
    const PlayBrick *pb = &play->bricks[index];
    int h, dx, layers = brick_layers(&play->scene->bricks[index]);

    for (h = 0; h < layers; h++) {
        for (dx = 0; dx < 2; dx++) {
            Cell at = { pb->cell_x + dx, pb->cell_y + h, pb->cell_z };
            if (cell_valid(at) && play->grid[at.x][at.y][at.z] == index + 1)
                play->grid[at.x][at.y][at.z] = 0;
        }
    }
}

/* Take a brick away, score it and count it (0x403cc0 / 0x403d20). */
static void prize_roll(Play *play, Vec3 at, PrizeKind kind);
static float random_range(float low, float range);

/*
 * A burst of puffs of one colour (0x41f330): 16 of them, fewer when many
 * are already about, thrown up and out a little, each 0.5 to 0.75 across.
 */
static void particle_burst(Play *play, Vec3 at, uint32_t color)
{
    int n = play->particle_count > 256 ? 4 : play->particle_count > 128 ? 8 : play->particle_count > 64 ? 12 : 16;

    while (n-- > 0 && play->particle_count < PLAY_MAX_PARTICLES) {
        Particle *p = &play->particles[play->particle_count++];
        p->position = p->from = at;
        p->velocity = vec3(random_range(-0.15f, 0.3f), random_range(0.0f, 0.15f), random_range(-0.15f, 0.3f));
        p->color = color;
        p->size = random_range(0.5f, 0.25f);
    }
}

/*
 * A fog brick (0x41f1a0) puffs smoke in its record's colour from just
 * above itself every 25 ms of its own clock (0x4242a0): drifting a little,
 * rising, 1 to 1.5 across, for 2 s.
 */
static void fog_update(Play *play, int index, uint32_t ms)
{
    const SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];
    const uint8_t *raw = b->object->raw;
    Particle *p;

    pb->idle_ms += ms;
    if (pb->idle_ms <= pb->call_ms)
        return;
    pb->call_ms = pb->idle_ms + 25u;
    if (play->smoke_count >= PLAY_MAX_SMOKE)
        return;
    p = &play->smoke[play->smoke_count++];
    p->position = p->from = vec3_add(b->position, vec3(0.0f, 1.0f, 0.0f));
    p->velocity = vec3(random_range(-0.05f, 0.1f), random_range(0.1f, 0.15f), random_range(-0.05f, 0.1f));
    p->color = ((uint32_t)raw[4] | (uint32_t)raw[5] << 8 | (uint32_t)raw[6] << 16) & 0xffffffu;
    p->life = 2000;
    p->size = random_range(1.0f, 0.5f);
}

/* A record's colour, bytes 4..7, as ARGB. */
static uint32_t record_color(const SceneBrick *b)
{
    const uint8_t *raw = b->object->raw;

    return (uint32_t)raw[4] | (uint32_t)raw[5] << 8 | (uint32_t)raw[6] << 16 | (uint32_t)raw[7] << 24;
}

/*
 * A firework brick hit by anything (0x4079c0, 0x407a70) leaves the grid,
 * counts as broken (no score, no prize) and fires a rocket from its cell
 * (object type 0xa8). The original forgets to count it when it is at the
 * heart of an explosion, which would leave the level unfinishable; here
 * it always counts.
 */
static void rocket_hit(Play *play, int index)
{
    SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];
    Firework *f;

    if (b->removed)
        return;
    b->removed = true;
    grid_remove(play, index);
    if (pb->counts)
        play->bricks_left--;
    brick_sound(play, index, b->animal->hit_sound);
    if (play->firework_count >= PLAY_MAX_FIREWORKS)
        return;
    f = &play->fireworks[play->firework_count++];
    f->position = f->from = vec3(floorf(b->position.x), (float)(int)b->position.y, (float)(int)b->position.z);
    f->velocity = vec3(0.0f, 1.8f, 0.0f);
    f->color = record_color(b);
    f->age = 0;
    f->spark_ms = 0;
}

/*
 * A firework's burst (0x40c2d0): 64 sparks thrown out in a ring tilted
 * 45 degrees (the original sets y and z both from the cosine), 1 to 1.8
 * across; one in three stays unseen until it fades (0x40c050).
 */
static void firework_burst(Play *play, Vec3 at, uint32_t color)
{
    int n;

    for (n = 0; n < 64 && play->spark_count < PLAY_MAX_SPARKS; n++) {
        Particle *p = &play->sparks[play->spark_count++];
        float a = random_range(0.0f, 6.2831855f);
        float r = random_range(0.5f, 0.25f);
        p->position = p->from = at;
        p->velocity = vec3(r * sinf(a), r * cosf(a), r * cosf(a));
        p->size = random_range(1.0f, 0.8f);
        p->color = rand() % 3 == 0 ? color & 0xffffffu : color;
        p->life = 1500 + rand() % 1000;
    }
}

/*
 * A rocket (0x40be50) slows as it climbs, leaving a spark in its colour
 * 1.25 below it every 25 ms; after 1.5 s it bursts with a bang.
 */
static bool firework_update(Play *play, Firework *f, uint32_t ms, float dt)
{
    f->age += ms;
    if (f->age > 1500) {
        firework_burst(play, vec3(floorf(f->position.x), (float)(int)f->position.y, (float)(int)f->position.z),
                       f->color);
        sound_at(play, f->position, "Firework2.ogg");
        return false;
    }
    f->position = vec3_add(f->position, vec3_scale(f->velocity, dt));
    f->velocity = vec3_sub(f->velocity, vec3_scale(f->velocity, 0.075f * dt));
    f->spark_ms += ms;
    if (f->spark_ms > 25) {
        f->spark_ms -= 25;
        if (play->trail_count < PLAY_MAX_TRAIL) {
            TrailSpot *spot = &play->trail[play->trail_count++];
            spot->position = vec3(f->position.x + random_range(-0.15f, 0.3f), f->position.y - 1.25f, f->position.z);
            spot->color = f->color;
            spot->size = 0.75f;
            spot->life = 250;
        }
    }
    return true;
}

/*
 * An animal hit (0x407eb0) leaves the grid, counts as broken and cries
 * out, then flies up rolling over. Blown up (0x408150) it also scores,
 * may drop a prize and leaves a burst of puffs in its record's colour.
 */
static void animal_hit(Play *play, int index, bool blown_up)
{
    SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];

    if (b->animal->kind == MODEL_ROCKET) {
        rocket_hit(play, index);
        return;
    }
    /* Teleporters leave the grid and go straight back (0x403cc0, 0x4087c0). */
    if (b->animal->kind != MODEL_ANIMAL)
        return;
    if (b->removed)
        return;
    b->removed = true;
    grid_remove(play, index);
    if (pb->counts)
        play->bricks_left--;
    if (blown_up) {
        prize_roll(play, b->position, PRIZE_NONE);
        play->score += pb->points * 100;
        particle_burst(play, vec3(floorf(b->position.x), (float)(int)b->position.y, (float)(int)b->position.z),
                       record_color(b));
    }
    brick_sound(play, index, b->animal->hit_sound);
    b->flying_ms = 1;
    b->flip = 0.0f;
    pb->climb = random_range(1.0f, 1.0f);
}

/*
 * A hit animal climbs faster and faster while its own weight pulls it down
 * 0.003 a ms, rolling over a quarter turn slowly then a quarter faster;
 * after 2 s it is gone (0x407f50). Until then it sometimes makes its sound
 * (every 4 to 19 s).
 */
static void animal_update(Play *play, int index, uint32_t ms, float dt)
{
    SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];

    if (b->flying_ms > 0) {
        b->flying_ms += ms;
        if (b->flying_ms >= 2000) {
            b->flying_ms = 0;
            return;
        }
        b->position.y += pb->climb * dt;
        pb->climb += 0.1f * dt;
        if (b->flip < 3.14159265f)
            b->flip += (b->flip < 1.5707964f ? 0.25f : 0.5f) * dt;
        b->position.y -= 0.003f * (float)ms;
        return;
    }
    if (b->removed)
        return;
    pb->idle_ms += ms;
    if (pb->idle_ms > pb->call_ms) {
        brick_sound(play, index, b->animal->idle_sound);
        pb->call_ms = pb->idle_ms + 4000u + (uint32_t)(rand() % 15000);
    }
}

static void brick_take(Play *play, int index)
{
    SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];

    if (b->animal != NULL) {
        animal_hit(play, index, true);
        return;
    }
    if (b->removed)
        return;
    b->removed = true;
    grid_remove(play, index);
    scene_break_brick(play->scene, index);
    if (pb->counts)
        play->bricks_left--;
    play->score += pb->points * 100;
    play->bricks_changed = true;
    /* Every level's bricks leave the prize to chance (kind 0xff in the file). */
    prize_roll(play, b->position, PRIZE_NONE);
}

static void brick_destroy(Play *play, int index);

/* An explosion breaks the brick it starts in and its neighbours (0x40feb0). */
static void explode(Play *play, Vec3 at)
{
    static const float offsets[][3] = {
        { -1.49f, 0, 0 }, { 1.51f, 0, 0 },
        { 0, 2, 0 }, { 1, 2, 0 }, { 0, -2, 0 }, { 1, -2, 0 },
        { 0, 0, -2 }, { 1, 0, -2 }, { 0, 0, 2 }, { 1, 0, 2 }
    };
    int self = brick_at(play, at), i;
    Vec3 p = at;

    /* (A cannon ball can burst on fire, which itself stays.) */
    if (self >= 0) {
        p = play->scene->bricks[self].position;
        brick_take(play, self);
    }
    for (i = 0; i < (int)(sizeof(offsets) / sizeof(offsets[0])); i++) {
        int n = brick_at(play, vec3(p.x + offsets[i][0], p.y + offsets[i][1], p.z + offsets[i][2]));
        if (n >= 0)
            brick_destroy(play, n);
    }
}

/* Break a brick outright, whatever hits it has left (vtable slot 0x4c). */
static void brick_destroy(Play *play, int index)
{
    const SceneBrick *b = &play->scene->bricks[index];

    if (b->removed)
        return;
    if (b->animal != NULL) {
        animal_hit(play, index, false);
        return;
    }
    switch (b->object->type) {
    case 0x20: /* explosive */
        brick_sound(play, index, "Explosive.ogg");
        explode(play, b->position);
        break;
    case 0x28:
        brick_sound(play, index, "Strong2.ogg");
        brick_take(play, index);
        break;
    case 0x18:
        brick_sound(play, index, "Glass2.ogg");
        brick_take(play, index);
        break;
    default:
        brick_sound(play, index, "Normal.ogg");
        brick_take(play, index);
        break;
    }
}

/*
 * The ball (or nothing, for a bullet) hits a brick. Returns true if the
 * ball should bounce off it, false if it carries on (vtable slot 0x50).
 */
static bool brick_hit(Play *play, int index, const Ball *ball)
{
    SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];
    uint32_t flags = ball != NULL ? ball->flags : 0;
    bool through = (flags & BALL_POWER) != 0;

    if (flags & BALL_EXPLOSIVE) {
        explode(play, b->position);
        return !through;
    }
    if (b->animal != NULL) { /* a powerball goes on through an animal (0x407e20), not a rocket (0x407990) */
        animal_hit(play, index, false);
        return b->animal->kind == MODEL_ROCKET || !through;
    }
    switch (b->object->type) {
    case 0x18: /* glass: shows on the first hit, breaks on the second */
        if (through) {
            brick_destroy(play, index);
            return false;
        }
        if (b->hidden) {
            b->hidden = false;
            play->bricks_changed = true;
            brick_sound(play, index, "Glass1.ogg");
        } else {
            brick_destroy(play, index);
        }
        return true;
    case 0x28: /* strong */
    case 0x30: /* stone */
        if (through) {
            brick_destroy(play, index);
            return false;
        }
        if (--pb->hits_left > 0) {
            brick_sound(play, index, b->object->type == 0x28 ? "Strong1.ogg" : "Stone.ogg");
            if (b->object->type == 0x28) { /* shows the next crack (0x40854b) */
                b->cracks++;
                play->bricks_changed = true;
            }
            return true;
        }
        brick_destroy(play, index);
        return true;
    default:
        brick_destroy(play, index);
        return !through;
    }
}

/*
 * The ball against the brick in its cell (0x403110): if it crossed one of
 * the brick's faces since the last step, that part of its velocity turns
 * round and it goes back to where it was.
 */
static bool crossed_face(const Ball *ball, Vec3 at, Vec3 *v)
{
    Vec3 p = ball->position, prev = ball->previous;

    *v = ball->velocity;
    if (p.z < at.z + 1.0f && prev.z > at.z + 1.0f)
        v->z = fabsf(v->z);
    else if (p.z > at.z - 1.0f && prev.z < at.z - 1.0f)
        v->z = -fabsf(v->z);
    else if (p.x > at.x - 0.99f && prev.x < at.x - 0.99f)
        v->x = -fabsf(v->x);
    else if (p.x < at.x + 1.01f && prev.x > at.x + 1.01f)
        v->x = fabsf(v->x);
    else
        return false;
    /* Only the bottom layer is in the ball's way. */
    return at.y <= 0.5f;
}

/* The fire or fountain in a point's cell, or -1. They stand in two cells, like bricks. */
static int billboard_at(const Play *play, Vec3 p)
{
    Cell c = cell_of(p);
    int i;

    for (i = 0; i < play->scene->billboard_count; i++) {
        Cell b = cell_of(play->scene->billboards[i].position);
        if ((c.x == b.x || c.x == b.x + 1) && c.y == b.y && c.z == b.z)
            return i;
    }
    return -1;
}

/*
 * Making a ball explosive (0x401d30): for some ms, or for good when ms is
 * 0; or plain again.
 */
static void ball_set_explosive(Ball *ball, bool on, uint32_t ms)
{
    if (on) {
        ball->flags = (ball->flags & ~(uint32_t)BALL_HYPNOTIC) | BALL_EXPLOSIVE;
        ball->explosive_end = ms != 0 ? ball->age + ms : PLAY_FOREVER;
    } else {
        ball->flags &= ~(uint32_t)(BALL_EXPLOSIVE | BALL_HYPNOTIC);
        ball->explosive_end = 0;
    }
}

/*
 * A ball goes through fire or a fountain (0x405d70, 0x405f10). Fire makes
 * it explosive for 5 s, unless it is explosive for good already; a
 * fountain puts out any explosive ball, even for good, in half a second.
 */
static void ball_vs_billboard(Play *play, Ball *ball, int index)
{
    const SceneBillboard *bb = &play->scene->billboards[index];
    Vec3 p = bb->position;
    int volume = (int)(100.0f - fabsf(p.z + p.z));
    const char *name;

    if (bb->water) {
        if (!(ball->flags & BALL_EXPLOSIVE))
            return;
        ball_set_explosive(ball, true, 500);
        name = "WaterBrick2.ogg";
    } else {
        if ((ball->flags & BALL_EXPLOSIVE) && ball->explosive_end == PLAY_FOREVER)
            return;
        ball_set_explosive(ball, true, 5000);
        name = "FireBrick2.ogg";
    }
    if (volume >= 0)
        sound(play, name, volume, (int)(p.x / play->scene->board.half_width * 100.0f));
}

/*
 * Turning a ball about the vertical by an angle (0x402010). The original
 * keeps the length of the whole velocity, climb and all, as its speed
 * across the board.
 */
static void ball_turn(Ball *ball, float angle)
{
    Vec3 v = ball->velocity;
    float a = atan2f(v.x, v.z) + angle;
    float speed = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);

    ball->velocity.x = sinf(a) * speed;
    ball->velocity.z = cosf(a) * speed;
}

/*
 * Sending a ball off in a new direction (0x402390): a nudge if it was sent
 * off less than 2 s ago, then a turn by anything up to half a circle.
 */
static void ball_new_direction(Ball *ball)
{
    uint32_t since = ball->age - ball->turn_time;

    if (since < 1000)
        ball_turn(ball, random_range(-0.5f, 1.0f));
    else if (since < 2000)
        ball_turn(ball, random_range(-0.8f, 1.6f));
    ball_turn(ball, random_range(-3.1415927f, 6.2831855f));
    ball->turn_time = ball->age;
}

/*
 * A teleporter's lightning (0x40d730) runs along a Hermite curve from
 * above it to above where the ball lands, leaving upwards and arriving
 * downwards, as a flat ribbon widest in the middle.
 */
static void bolt_add(Play *play, Vec3 from, Vec3 to)
{
    Vec3 d = vec3_sub(to, from);
    float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    Bolt *bolt;
    int k;

    if (play->bolt_count >= PLAY_MAX_BOLTS)
        return;
    bolt = &play->bolts[play->bolt_count++];
    bolt->time = 0;
    for (k = 0; k < BOLT_POINTS; k++) {
        float s = (float)(k + 1) * 0.05f; /* (sic: runs a step past the end) */
        float s2 = s * s, s3 = s2 * s;
        float h1 = 2.0f * s3 - 3.0f * s2 + 1.0f, h2 = -2.0f * s3 + 3.0f * s2;
        float h3 = s3 - 2.0f * s2 + s, h4 = s3 - s2;
        float width = 0.1f * (float)(k < 10 ? k : BOLT_POINTS - 1 - k);
        Vec3 p = vec3(h1 * from.x + h2 * to.x,
                      h1 * from.y + h2 * to.y + (h3 - h4) * 2.0f * length,
                      h1 * from.z + h2 * to.z);
        bolt->vertices[k * 2] = vec3(p.x + width, p.y, p.z);
        bolt->vertices[k * 2 + 1] = vec3(p.x - width, p.y, p.z);
    }
}

/* The lightning crackles, its inside points jumping about, and is gone after 2 s (0x40d5f0). */
static bool bolt_update(Bolt *bolt, uint32_t ms)
{
    int i;

    for (i = 1; i < BOLT_POINTS * 2 - 1; i++) {
        bolt->vertices[i].x += random_range(-0.025f, 0.05f);
        bolt->vertices[i].y += random_range(-0.025f, 0.05f);
        bolt->vertices[i].z += random_range(-0.025f, 0.05f);
    }
    bolt->time += ms;
    return bolt->time / 50 < 2 * (BOLT_POINTS - 1);
}

/*
 * A ball in a teleporter's cell (0x4087f0) is put at the cell in the
 * teleporter's record (bytes 6, 5 and 4: x, y and z), waits there 1 s and
 * then goes off in a new direction; lightning arcs from the teleporter.
 */
static void ball_teleport(Play *play, Ball *ball, int index)
{
    const SceneBrick *b = &play->scene->bricks[index];
    const uint8_t *raw = b->object->raw;
    Vec3 to = vec3((float)(int8_t)raw[6], (float)raw[5], (float)(int8_t)raw[4]);
    Vec3 from = vec3(b->position.x + 0.5f, b->position.y + 6.0f, b->position.z);

    ball->position = to;
    ball->flags |= BALL_TELEPORTED;
    ball->wait_end = ball->age + 1000;
    ball_new_direction(ball);
    /* Both ends go through an object record: the start is snapped to the
       grid (0x40dd50), and both are cut to whole numbers. */
    from = vec3(floorf(from.x), 2.0f * floorf(from.y * 0.5f), 2.0f * floorf(from.z * 0.5f) + 1.0f);
    bolt_add(play, from, vec3((float)(int)(to.x + 0.5f), (float)(int)(to.y + 4.0f), (float)(int)to.z));
    brick_sound(play, index, "Teleport.ogg");
}

static void ball_vs_brick(Play *play, Ball *ball)
{
    int index = brick_at(play, ball->position);
    Vec3 v;

    if (index >= 0 && play->scene->bricks[index].animal != NULL &&
        play->scene->bricks[index].animal->kind >= MODEL_TELEPORT) {
        /* A teleporter takes the ball wherever it is in its cell (0x402354);
           where it arrives lets it go on through. */
        if (play->scene->bricks[index].animal->kind == MODEL_TELEPORT)
            ball_teleport(play, ball, index);
        return;
    }
    if (index < 0) {
        /* Fire and fountains let the ball through. */
        index = billboard_at(play, ball->position);
        if (index >= 0 && crossed_face(ball, play->scene->billboards[index].position, &v))
            ball_vs_billboard(play, ball, index);
        return;
    }
    if (!crossed_face(ball, play->scene->bricks[index].position, &v))
        return;
    if (brick_hit(play, index, ball)) {
        ball->position = ball->previous;
        ball->velocity = v;
    }
}

/* ------------------------------------------------------------------------ */
/* Falling bricks                                                           */
/* ------------------------------------------------------------------------ */

enum {
    SUPPORT_RIGHT = 0x01,       /* (x+1, y-1) */
    SUPPORT_LEFT = 0x02,        /* (x, y-1) */
    SUPPORT_OUT_LEFT = 0x04,    /* (x-1, y) */
    SUPPORT_DOWN_LEFT = 0x08,   /* (x-1, y-1) */
    SUPPORT_OUT_RIGHT = 0x10,   /* (x+2, y) */
    SUPPORT_DOWN_RIGHT = 0x20,  /* (x+2, y-1) */
    SUPPORT_ON_LEFT = 0x40,     /* (x, y+1) */
    SUPPORT_ON_RIGHT = 0x80,    /* (x+1, y+1) */
    SUPPORT_EDGE_LEFT = 0x100,
    SUPPORT_EDGE_RIGHT = 0x200,
    SUPPORT_FLOOR = 0x400
};

/* Is a cell taken by a brick other than index? Outside the grid counts as taken. */
static bool taken(const Play *play, int index, int x, int y, int z)
{
    Cell c = { x, y, z };
    int at;
    if (!cell_valid(c))
        return true;
    at = play->grid[x][y][z];
    return at != 0 && at != index + 1;
}

/* What is around a brick at cell (x, y, z) (0x40de00). */
static unsigned support(const Play *play, int index, int x, int y, int z)
{
    int top = y + brick_layers(&play->scene->bricks[index]);
    unsigned bits = 0;

    if (y < 1)
        return SUPPORT_FLOOR;
    if (taken(play, index, x, y - 1, z)) bits |= SUPPORT_LEFT;
    if (taken(play, index, x + 1, y - 1, z)) bits |= SUPPORT_RIGHT;
    if (x < 1) bits |= SUPPORT_EDGE_LEFT;
    if (x + 2 >= GRID_X) bits |= SUPPORT_EDGE_RIGHT;
    if (taken(play, index, x - 1, y - 1, z)) bits |= SUPPORT_DOWN_LEFT;
    if (taken(play, index, x - 1, y, z)) bits |= SUPPORT_OUT_LEFT;
    if (taken(play, index, x + 2, y - 1, z)) bits |= SUPPORT_DOWN_RIGHT;
    if (taken(play, index, x + 2, y, z)) bits |= SUPPORT_OUT_RIGHT;
    if (top < GRID_Y && play->grid[x][top][z] != 0) bits |= SUPPORT_ON_LEFT;
    if (top < GRID_Y && x + 1 < GRID_X && play->grid[x + 1][top][z] != 0) bits |= SUPPORT_ON_RIGHT;
    return bits;
}

/*
 * A brick with nothing under it falls a layer; one held up on one side
 * only tips off that side, unless something on top holds it (0x402bb0).
 */
static void brick_gravity(Play *play, int index)
{
    const SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];
    unsigned bits;
    int dx = 0;

    /* Not while it is still on its way somewhere (0x402750). */
    if (b->removed || b->position.x != pb->target_x || b->position.y - pb->target_y > 0.5f)
        return;
    bits = support(play, index, pb->cell_x, pb->cell_y, pb->cell_z);
    if (bits & SUPPORT_FLOOR)
        return;
    if ((bits & (SUPPORT_LEFT | SUPPORT_RIGHT)) == 0) {
        dx = 0;
    } else if (brick_layers(b) == 1 && !(bits & SUPPORT_EDGE_LEFT) &&
               (bits & (SUPPORT_LEFT | SUPPORT_OUT_LEFT | SUPPORT_DOWN_LEFT)) == 0 &&
               (bits & (SUPPORT_ON_LEFT | SUPPORT_ON_RIGHT)) != SUPPORT_ON_RIGHT) {
        dx = -1;
    } else if (brick_layers(b) == 1 && !(bits & SUPPORT_EDGE_RIGHT) &&
               (bits & (SUPPORT_RIGHT | SUPPORT_OUT_RIGHT | SUPPORT_DOWN_RIGHT)) == 0 &&
               (bits & (SUPPORT_ON_LEFT | SUPPORT_ON_RIGHT)) != SUPPORT_ON_LEFT) {
        dx = 1;
    } else {
        return;
    }

    grid_remove(play, index);
    pb->cell_x += dx;
    pb->cell_y -= 1;
    pb->target_x += (float)dx;
    pb->target_y -= 2.0f;
    if (dx != 0)
        play->scene->bricks[index].tip = dx < 0 ? 1.5707964f : -1.5707964f;
    grid_place(play, index);
}

/*
 * The drawn brick catches up with where it is (0x402e40): a tipping brick
 * turns 0.005 radians a ms and then steps into its new place; otherwise it
 * falls 0.003 units a ms.
 */
static void brick_move(Play *play, int index, uint32_t ms)
{
    SceneBrick *b = &play->scene->bricks[index];
    const PlayBrick *pb = &play->bricks[index];
    float turn = 0.005f * (float)ms;

    if (b->animal != NULL && b->removed) /* flying off: animal_update moves it */
        return;
    if (b->tip > 0.0f) {
        b->tip -= turn;
        if (b->tip <= 0.0f) {
            b->tip = 0.0f;
            b->roll += 1.5707964f;
            b->position.x -= 1.0f;
            b->position.y -= 1.0f;
        }
        play->bricks_changed = true;
    } else if (b->tip < 0.0f) {
        b->tip += turn;
        if (b->tip >= 0.0f) {
            b->tip = 0.0f;
            b->roll -= 1.5707964f;
            b->position.x += 1.0f;
            b->position.y -= 1.0f;
        }
        play->bricks_changed = true;
    } else if (b->position.y > pb->target_y) {
        b->position.y -= 0.003f * (float)ms;
        if (b->position.y < pb->target_y)
            b->position.y = pb->target_y;
        play->bricks_changed = true;
    }
}

/* ------------------------------------------------------------------------ */
/* Balls                                                                    */
/* ------------------------------------------------------------------------ */

static float speed_of(Vec3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

/* Set the velocity, keeping it from going too flat (0x401f90). */
static void ball_set_velocity(Ball *ball, Vec3 v)
{
    /* A ball on the rubber bat's string may go as flat as it likes. */
    if (ball->flags & BALL_YOYO) {
        ball->velocity = v;
        return;
    }
    if (v.z >= 0.0f) {
        if (v.z < MIN_SPEED_Z)
            v.z = MIN_SPEED_Z;
    } else if (v.z > -MIN_SPEED_Z) {
        v.z = -MIN_SPEED_Z;
    }
    ball->velocity = v;
}

/* Point the ball at an angle from straight ahead, same speed (0x401a70). */
static void ball_set_angle(Ball *ball, float angle)
{
    float speed = speed_of(ball->velocity);
    ball->velocity.x = sinf(angle) * speed;
    ball->velocity.z = cosf(angle) * speed;
}

/* Speed up by amount, within [0.85, 2] x the board's scalex (0x401af0). */
static void ball_speed_up(const Play *play, Ball *ball, float amount)
{
    float max = play->scene->board.scale_x * 2.0f;
    float min = play->scene->board.scale_x * 0.85f;
    float speed = speed_of(ball->velocity), wanted = speed + amount;

    if (speed <= 0.0f)
        return;
    if (wanted > max) wanted = max;
    if (wanted < min) wanted = min;
    ball->velocity = vec3_scale(ball->velocity, wanted / speed);
}

static void ball_stick(Play *play, Ball *ball, float offset)
{
    ball->flags |= BALL_STUCK;
    ball->stuck_offset = offset;
    ball->position.z = play->bat.position.z + STUCK_Z;
}

static Ball *ball_add(Play *play, Vec3 at, bool stuck)
{
    Ball *ball;
    float s = play->scene->board.scale_x;

    if (play->ball_count >= PLAY_MAX_BALLS)
        return NULL;
    ball = &play->balls[play->ball_count++];
    memset(ball, 0, sizeof(*ball));
    ball->position = ball->previous = ball->from = at;
    ball->velocity = vec3(-0.35f * s, 0.0f, 0.65f * s);
    if (stuck)
        ball_stick(play, ball, 0.0f);
    return ball;
}

/* (rand() % 10000) * 0.0001 * range + low, the original's random range (0x42cf20). */
static float random_range(float low, float range)
{
    return low + (float)(rand() % 10000) * 0.0001f * range;
}

/* A ball's colour, for its trail: blue, red when explosive, yellow when hypnotic. */
static uint32_t ball_color(const Ball *ball)
{
    if (ball->flags & BALL_EXPLOSIVE)
        return 0xffff0000u;
    if (ball->flags & BALL_HYPNOTIC)
        return 0xffffff00u;
    return 0xff0000ffu;
}

/*
 * A powerball leaves a spot of its colour every 25 ms, scattered a little
 * when it is explosive too (0x401990).
 */
static void ball_trail(Play *play, Ball *ball)
{
    TrailSpot *spot;
    Vec3 p = ball->position;

    if (!(ball->flags & BALL_POWER) || ball->trail_time >= ball->age)
        return;
    ball->trail_time = ball->age + 25;
    if (play->trail_count >= PLAY_MAX_TRAIL)
        return;
    if (ball->flags & BALL_EXPLOSIVE) {
        p.x += random_range(-0.2f, 0.4f);
        p.y += random_range(-0.2f, 0.4f);
        p.z += random_range(-0.2f, 0.4f);
    }
    spot = &play->trail[play->trail_count++];
    spot->position = p;
    spot->color = ball_color(ball);
    spot->size = 1.25f;
    spot->life = 250;
}

/*
 * The rubber bat's string pulls a ball more than 10 away back towards the
 * bat, harder the further it is (0x401b80).
 */
static void yoyo_pull(const Play *play, Ball *ball, float dt)
{
    Vec3 d = vec3_sub(play->bat.position, ball->position);
    float distance = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

    if (distance < 10.0f)
        return;
    ball->velocity = vec3_add(ball->velocity, vec3_scale(d, distance * dt * 0.00025f));
    ball->velocity.y = 0.0f;
}

/* One step of a ball's own movement (0x402060). */
static void ball_update(Play *play, Ball *ball, uint32_t ms, float dt)
{
    ball->age += ms;
    ball->spin += 0.03f;
    /* A hypnotic ball sways from side to side (0x402092). */
    if (ball->flags & BALL_HYPNOTIC) {
        ball->wobble += 0.35f * dt;
        ball->velocity = mat4_transform_point(mat4_rotation_y(sinf(ball->wobble) * 0.01f),
                                              ball->velocity);
    }
    /* A fire's explosive wears off (0x402161). */
    if ((ball->flags & BALL_EXPLOSIVE) && ball->explosive_end != PLAY_FOREVER &&
        ball->age >= ball->explosive_end)
        ball_set_explosive(ball, false, 0);
    ball_trail(play, ball);
    ball->previous = ball->position;
    if (ball->flags & BALL_STUCK) {
        ball->position.x = play->bat.position.x + ball->stuck_offset;
        ball->position.y = BALL_Y;
        return;
    }
    /* A teleported ball waits, a step past its second (0x402212). */
    if (ball->flags & BALL_TELEPORTED) {
        if (ball->age > ball->wait_end)
            ball->flags &= ~(uint32_t)BALL_TELEPORTED;
        return;
    }
    if (ball->flags & BALL_YOYO)
        yoyo_pull(play, ball, dt);
    ball->position = vec3_add(ball->position, vec3_scale(ball->velocity, dt));
    ball->position.y = BALL_Y;
    ball_vs_brick(play, ball);
}

/* A ball against a round wall: mirror it about the circle's normal (0x406ca1). */
static bool round_wall(Ball *ball, Vec3 centre, float radius)
{
    float dx = ball->position.x - centre.x, dz = ball->position.z - centre.z;
    float length, nx, nz, dot;
    Vec3 v = ball->velocity;

    if (sqrtf(dx * dx + dz * dz) <= radius)
        return false;
    length = sqrtf(dx * dx + dz * dz);
    nx = dx / length;
    nz = dz / length;
    dot = v.x * nx + v.z * nz;
    v.x -= 2.0f * dot * nx;
    v.z -= 2.0f * dot * nz;
    ball->position = ball->previous;
    ball->velocity = v;
    return true;
}

/*
 * The bat blows up (0x4043ff): ten explosions along it, and its ten parts
 * fly off spinning, fading out over 1.5 s.
 */
static void bat_explode(Play *play)
{
    Bat *bat = &play->bat;
    int i;

    for (i = 0; i < 10; i++) {
        /* The factory gets whole-number positions. */
        /* Around the model's origin, 0.25 down and 1.25 back from the bat's point. */
        Vec3 p = vec3((float)(int)(bat->position.x + random_range(-bat->half_width, 2.0f * bat->half_width)),
                      (float)(int)(bat->position.y - 0.25f),
                      (float)(int)(bat->position.z - 1.25f + random_range(-1.0f, 2.0f)));
        scene_add_explosion(play->scene, p);
    }
    for (i = 0; i < BAT_PIECES; i++) {
        BatPiece *piece = &bat->pieces[i];
        piece->position = piece->from = vec3(0.0f, 0.0f, 0.0f);
        piece->velocity = vec3(random_range(-0.1f, 0.2f), random_range(0.3f, 0.3f),
                               random_range(-0.1f, 0.1f));
        piece->axis = vec3(random_range(-0.3f, 0.6f), random_range(0.3f, 0.3f),
                           random_range(-0.3f, 0.6f));
        piece->angle = 0.0f;
        piece->spin = random_range(0.3f, 0.3f);
    }
    bat->pieces_ms = 1500;
}

/* The bat's sounds are panned by where it is (0x4047c0). */
static void bat_sound(Play *play, const char *name)
{
    sound(play, name, -1, (int)(play->bat.position.x / play->scene->board.half_width * 100.0f));
}

/* Release every ball held by the bat (0x40e680). */
static void release_balls(Play *play)
{
    int i;
    for (i = 0; i < play->ball_count; i++)
        play->balls[i].flags &= ~(uint32_t)BALL_STUCK;
}

/* The rubber bat lets its balls go, back within the usual speeds (0x40e6b0). */
static void release_yoyo(Play *play)
{
    int i;
    for (i = 0; i < play->ball_count; i++) {
        Ball *ball = &play->balls[i];
        if (ball->flags & BALL_YOYO) {
            ball->flags &= ~(uint32_t)BALL_YOYO;
            ball_speed_up(play, ball, 0.0f);
        }
    }
}

/* The bat dies, from losing the last ball or catching a bat killer (0x4043d0 with 0x40). */
static void bat_kill(Play *play)
{
    if (play->bat.mode == BAT_DEAD)
        return;
    sound(play, "batkilled.ogg", -1, 0);
    bat_explode(play);
    release_balls(play);
    play->bat.mode = BAT_DEAD;
}

/*
 * The bat becomes a magnet, gun and so on (0x4043d0). Leaving the magnet
 * lets go of the balls it holds.
 */
static void bat_set_mode(Play *play, BatMode mode)
{
    Bat *bat = &play->bat;

    if (mode == BAT_DEAD) {
        bat_kill(play);
        return;
    }
    if (bat->mode == BAT_MAGNET && mode != BAT_MAGNET)
        release_balls(play);
    if (bat->mode == BAT_RUBBER && mode != BAT_RUBBER)
        release_yoyo(play);
    switch (mode) {
    case BAT_MAGNET: bat->ammo = 2; break; /* never used up */
    case BAT_GUN: bat->ammo = 18; break;   /* two a shot */
    case BAT_BAZOOKA: bat->ammo = 8; break;
    case BAT_CANNON: bat->ammo = 5; break;
    case BAT_RUBBER: bat->rubber_end = play->time + 30000; break;
    default: break;
    }
    bat->mode = mode;
    bat->weapon_time = 0;
}

/* A prize grows or shrinks the bat by this much, within [2, 4.5] (0x403f90). */
static void bat_resize(Bat *bat, float amount)
{
    bat->target_width += amount;
    if (bat->target_width < BAT_MIN_WIDTH)
        bat->target_width = BAT_MIN_WIDTH;
    if (bat->target_width > BAT_MAX_WIDTH)
        bat->target_width = BAT_MAX_WIDTH;
}

/* Each frame: gravity 0.001 a ms, then a move by the whole velocity (0x4048d8). */
static void bat_pieces_step(Bat *bat, uint32_t ms)
{
    int i;

    if (bat->pieces_ms == 0)
        return;
    for (i = 0; i < BAT_PIECES; i++) {
        BatPiece *piece = &bat->pieces[i];
        piece->velocity.y -= 0.001f * (float)ms;
        piece->position = vec3_add(piece->position, piece->velocity);
        piece->angle += (float)ms * piece->spin * 0.0025f;
    }
    bat->pieces_ms = bat->pieces_ms > ms ? bat->pieces_ms - ms : 0;
}

/* A ball against the bat; true if it bounced (0x405450). */
static bool ball_vs_bat(Play *play, Ball *ball)
{
    Bat *bat = &play->bat;
    float d;

    if (play->state != PLAY_RUNNING || bat->mode == BAT_DEAD)
        return false;
    if (ball->position.x < bat->position.x - bat->half_width ||
        ball->position.x >= bat->position.x + bat->half_width)
        return false;
    d = ball->position.x - bat->position.x;
    /* The magnet holds the ball where it hit, then it bounces as usual
       when let go. */
    if (bat->mode == BAT_MAGNET && bat->ammo > 0) {
        ball_stick(play, ball, d);
        bat_sound(play, "magnited.ogg");
    }
    /* The model's bounce: keys 1-150 in the middle, 1000-1150 at the
       right end, 1150-1300 at the left (0x4054f3). */
    if (d > bat->half_width - 1.0f) {
        bat->bounce_kind = 1;
        bat->bounce_time = 1000;
        bat->bounce_ms = 0;
    } else if (d < -(bat->half_width - 1.0f)) {
        bat->bounce_kind = 2;
        bat->bounce_time = 1150;
        bat->bounce_ms = 0;
    } else {
        bat->bounce_kind = 0;
        bat->bounce_time = 1;
        bat->bounce_ms = 0;
    }
    ball_set_angle(ball, BAT_ANGLE / bat->half_width * d);
    ball_speed_up(play, ball, 0.01f);
    bat_sound(play, "ball2.ogg");
    /* The rubber bat ties the ball to it on a string. */
    if (bat->mode == BAT_RUBBER) {
        ball->flags |= BALL_YOYO;
        bat_sound(play, "rubbered.ogg");
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Prizes                                                                   */
/* ------------------------------------------------------------------------ */

static const char *const k_prize_names[PRIZE_KINDS] = {
    "", "incbat", "decbat", "topowerfull", "tonormal", "tosimple", "toexplosive",
    "togipnoz", "speedup", "speeddown", "trippleball", "magnit", "gun", "bazuka",
    "cannon", "rubber", "nextlevel", "killbat", "life"
};

const char *play_prize_name(PrizeKind kind)
{
    return kind > PRIZE_NONE && kind < PRIZE_KINDS ? k_prize_names[kind] : "";
}

/* Prizes/<name>.txt for every kind (0x4201b0). */
static void load_prize_info(Play *play)
{
    int k;

    for (k = PRIZE_NONE + 1; k < PRIZE_KINDS; k++) {
        PrizeInfo *info = &play->prize_info[k];
        char path[64];
        Config cfg;

        snprintf(path, sizeof(path), "Prizes/%s.txt", k_prize_names[k]);
        if (!config_load(&cfg, path)) {
            log_warn("can't load %s", path);
            continue;
        }
        info->loaded = true;
        info->score = config_get_int(&cfg, "score", 0);
        snprintf(info->sound, sizeof(info->sound), "%s", config_get_string(&cfg, "sound", ""));
        snprintf(info->model, sizeof(info->model), "%s", config_get_string(&cfg, "grdata", ""));
        config_get_floats(&cfg, "glowsize", info->glow_size, 2);
        config_get_floats(&cfg, "glowoffset", info->glow_offset, 3);
        info->color = config_get_color(&cfg, "messagecolor", 0xffffffffu);
        info->color = config_get_color(&cfg, "glowcolor", info->color);
        config_free(&cfg);
    }
}

/*
 * A new prize falls out of a brick (0x4117c0): up and towards the bat,
 * more gently close to the bat, and a little faster or slower at random
 * (0x41f800, 0x41fde0).
 */
static bool prize_add(Play *play, PrizeKind kind, Vec3 at)
{
    Prize *prize;

    if (play->prize_count >= PLAY_MAX_PRIZES)
        return false;
    prize = &play->prizes[play->prize_count++];
    memset(prize, 0, sizeof(*prize));
    prize->kind = kind;
    prize->position = prize->from = at;
    prize->velocity = prize->bounce = vec3(0.0f, 0.75f, -0.65f);
    prize->scale = 1.0f;
    prize->glow = 1.0f;
    if (at.z - play->bat.position.z < 5.0f)
        prize->velocity = vec3(0.0f, 0.5f, -0.45f);
    prize->velocity = vec3_scale(prize->velocity, random_range(0.85f, 0.3f));
    return true;
}

/*
 * Maybe drop a prize (0x411890). PRIZE_NONE picks one at random: rarely,
 * twice as often after five quiet seconds, never within a second of the
 * last one, and never the same kind twice running.
 */
static void prize_roll(Play *play, Vec3 at, PrizeKind kind)
{
    static const struct { int low, high; PrizeKind kind; } odds[] = {
        { 0, 80, PRIZE_INCBAT }, { 100, 170, PRIZE_DECBAT },
        { 200, 260, PRIZE_TOPOWERFULL }, { 300, 360, PRIZE_TONORMAL },
        { 400, 450, PRIZE_TOSIMPLE }, { 500, 570, PRIZE_TOEXPLOSIVE },
        { 600, 670, PRIZE_TOGIPNOZ }, { 700, 710, PRIZE_KILLBAT },
        { 800, 805, PRIZE_LIFE }, { 2000, 2080, PRIZE_SPEEDUP },
        { 2100, 2170, PRIZE_SPEEDDOWN }, { 2200, 2260, PRIZE_TRIPPLEBALL },
        { 2900, 2960, PRIZE_RUBBER }, { 3000, 3080, PRIZE_GUN },
        { 3100, 3170, PRIZE_BAZUKA }, { 3200, 3260, PRIZE_CANNON },
        { 3300, 3370, PRIZE_MAGNIT }
    };
    uint32_t since;

    if (play->prize_time > play->time)
        return;
    since = play->time - play->prize_time;
    if (since > 3000)
        play->last_prize = PRIZE_NONE;
    if (kind == PRIZE_NONE) {
        int r = rand() % (since > 5000 ? 4000 : 8000), i;
        for (i = 0; i < (int)(sizeof(odds) / sizeof(odds[0])); i++) {
            if (r > odds[i].low && r < odds[i].high)
                kind = odds[i].kind;
        }
        if (kind == PRIZE_NONE)
            return;
    }
    if (kind == play->last_prize)
        return;
    play->last_prize = kind;
    if (prize_add(play, kind, at))
        play->prize_time = play->time + 1000;
}

/* Three balls from each one: two more, 120 degrees either side (0x411f20). */
static void triple_balls(Play *play)
{
    int count = play->ball_count, i, side;

    for (i = 0; i < count; i++) {
        for (side = -1; side <= 1; side += 2) {
            Ball *from = &play->balls[i], *ball = ball_add(play, from->position, false);
            float angle = atan2f(from->velocity.x, from->velocity.z);

            if (ball == NULL)
                return;
            ball->velocity = from->velocity;
            ball_set_angle(ball, angle + (float)side * 2.0944f);
            /* It takes on the ball's powers (0x401f00). */
            ball->flags = from->flags & (BALL_POWER | BALL_EXPLOSIVE | BALL_HYPNOTIC);
            ball->explosive_end = from->explosive_end;
        }
    }
}

/* The bat caught a prize (0x41fae0). */
static void prize_apply(Play *play, const Prize *prize)
{
    const PrizeInfo *info = &play->prize_info[prize->kind];
    int i;

    /* Panned hard left, centre or right: the original rounds before scaling. */
    if (info->sound[0] != '\0')
        sound(play, info->sound, -1, (int)(prize->position.x / play->scene->board.half_width) * 100);
    play->score += info->score;

    switch (prize->kind) {
    case PRIZE_INCBAT: bat_resize(&play->bat, 0.5f); break;
    case PRIZE_DECBAT: bat_resize(&play->bat, -0.5f); break;
    case PRIZE_TOPOWERFULL:
    case PRIZE_TONORMAL:
        for (i = 0; i < play->ball_count; i++) {
            if (prize->kind == PRIZE_TOPOWERFULL)
                play->balls[i].flags |= BALL_POWER;
            else
                play->balls[i].flags &= ~(uint32_t)BALL_POWER;
        }
        break;
    case PRIZE_TOEXPLOSIVE:
    case PRIZE_TOSIMPLE:
        for (i = 0; i < play->ball_count; i++)
            ball_set_explosive(&play->balls[i], prize->kind == PRIZE_TOEXPLOSIVE, 0);
        break;
    case PRIZE_SPEEDUP:
    case PRIZE_SPEEDDOWN:
        for (i = 0; i < play->ball_count; i++)
            ball_speed_up(play, &play->balls[i], prize->kind == PRIZE_SPEEDUP ? 0.25f : -0.25f);
        break;
    case PRIZE_TRIPPLEBALL: triple_balls(play); break;
    case PRIZE_TOGIPNOZ:
        /* Hypnotic for 15 s, for every ball (0x40e750). */
        for (i = 0; i < play->ball_count; i++)
            play->balls[i].flags = (play->balls[i].flags & ~(uint32_t)BALL_EXPLOSIVE) | BALL_HYPNOTIC;
        play->hypnotic_end = play->time + 15000;
        break;
    case PRIZE_MAGNIT: bat_set_mode(play, BAT_MAGNET); break;
    case PRIZE_GUN: bat_set_mode(play, BAT_GUN); break;
    case PRIZE_BAZUKA: bat_set_mode(play, BAT_BAZOOKA); break;
    case PRIZE_CANNON: bat_set_mode(play, BAT_CANNON); break;
    case PRIZE_RUBBER: bat_set_mode(play, BAT_RUBBER); break;
    case PRIZE_KILLBAT: bat_set_mode(play, BAT_DEAD); break;
    case PRIZE_LIFE:
        if (play->lives < 9)
            play->lives++;
        break;
    case PRIZE_NEXTLEVEL: play->bricks_left = 0; break;
    default:
        break;
    }
}

/*
 * A prize's step (0x41f910): it flies, bounces lower each time it lands,
 * and shrinks away once it is past the bat. Then the bat may catch it
 * (0x404730). Returns false when it is gone.
 */
static bool prize_update(Play *play, Prize *prize, uint32_t ms, float dt)
{
    const Bat *bat = &play->bat;
    Vec3 p;

    prize->age += ms;
    prize->position = vec3_add(prize->position, vec3_scale(prize->velocity, dt));
    if (prize->position.y <= -0.5f) {
        prize->position.y = -0.49f;
        if (prize->bounce.y >= 0.1f) {
            prize->bounce = vec3_scale(prize->bounce, 0.75f);
            prize->velocity = prize->bounce;
            if (prize->velocity.z > -0.25f)
                prize->velocity.z = -0.25f;
        } else {
            prize->velocity.y = 0.0f;
        }
    } else {
        prize->velocity.y -= 0.1f * dt;
    }
    if (bat->position.z > prize->position.z) {
        prize->scale -= 0.2f * dt;
        prize->glow *= 1.0f - 0.3f * dt;
        if (prize->scale < 0.25f)
            return false;
    }

    p = prize->position;
    if (bat->mode == BAT_DEAD || p.z > bat->position.z + 1.0f)
        return true;
    if (p.z < bat->position.z - 3.0f)
        return false;
    if (p.x < bat->position.x - bat->half_width || p.x > bat->position.x + bat->half_width)
        return true;
    prize_apply(play, prize);
    return false;
}

void play_drop_prize(Play *play, PrizeKind kind, Vec3 at)
{
    if (kind > PRIZE_NONE && kind < PRIZE_KINDS)
        prize_add(play, kind, at);
}

/* ------------------------------------------------------------------------ */
/* The bat's weapons                                                        */
/* ------------------------------------------------------------------------ */

/* Compare two words ignoring case, like strcasecmp. */
static int compare_words(const char *a, const char *b)
{
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/*
 * A Bullets/ file (0x4094f0). The keys act in the file's order: "speed"
 * scales the velocity so far, starting at (0, 0, 0.5), and "vertspeed"
 * sets its upward part.
 */
static void load_bullet_info(BulletInfo *info, const char *path)
{
    Config cfg;
    int i;

    memset(info, 0, sizeof(*info));
    info->kind = BULLET_BULLET;
    info->velocity = vec3(0.0f, 0.0f, 0.5f);
    info->gravity = 0.1f;
    info->shadow_size[0] = info->shadow_size[1] = info->shadow_size[2] = 1.0f;
    info->shadow_color = 0xffffffffu;
    if (!config_load(&cfg, path)) {
        log_warn("can't load %s", path);
        return;
    }
    info->loaded = true;
    for (i = 0; i < cfg.count; i++) {
        const char *key = cfg.entries[i].key, *value = cfg.entries[i].value;
        float f[3] = { 0, 0, 0 };

        if (value == NULL)
            continue;
        if (compare_words(key, "gravity") == 0) {
            config_parse_floats(value, &info->gravity, 1);
        } else if (compare_words(key, "vertspeed") == 0) {
            config_parse_floats(value, f, 1);
            info->velocity.y = f[0];
        } else if (compare_words(key, "speed") == 0) {
            float length = sqrtf(vec3_dot(info->velocity, info->velocity));
            config_parse_floats(value, f, 1);
            if (length > 0.0f) {
                info->velocity.x *= f[0] / length;
                info->velocity.z *= f[0] / length;
            }
        } else if (compare_words(key, "shadowsize") == 0) {
            config_parse_floats(value, info->shadow_size, 3);
        } else if (compare_words(key, "shadowcolor") == 0) {
            info->shadow_color = (uint32_t)strtoul(value, NULL, 16);
        } else if (compare_words(key, "type") == 0) {
            if (compare_words(value, "rocket") == 0)
                info->kind = BULLET_ROCKET;
            else if (compare_words(value, "yadro") == 0)
                info->kind = BULLET_YADRO;
            else if (compare_words(value, "smallcannonbullet") == 0)
                info->kind = BULLET_SMALL_CANNON;
        } else if (compare_words(key, "grdata") == 0) {
            snprintf(info->model, sizeof(info->model), "%s", value);
        }
    }
    config_free(&cfg);
}

/* The pad file names what each weapon fires, and how often (0x405910). */
static void load_weapons(Play *play)
{
    static const char *const keys[WEAPON_COUNT][2] = {
        { "bullets", "bulletdelay" }, { "rockets", "rocketdelay" }, { "torpedos", "torpedodelay" }
    };
    const char *pad = play->scene->level.slots[LEVEL_SLOT_PAD];
    Config cfg;
    int w;

    if (!config_load(&cfg, pad[0] ? pad : "Pads/common.txt"))
        return;
    for (w = 0; w < WEAPON_COUNT; w++) {
        load_bullet_info(&play->bullet_info[w], config_get_string(&cfg, keys[w][0], ""));
        play->bullet_info[w].delay = config_get_int(&cfg, keys[w][1], 0);
    }
    config_free(&cfg);
}

/* A shot leaves the bat straight ahead (0x411610, 0x409430). */
static void fire(Play *play, Weapon weapon, Vec3 at)
{
    const BulletInfo *info = &play->bullet_info[weapon];
    Bullet *bullet;
    Vec3 v = info->velocity;

    if (play->bullet_count >= PLAY_MAX_BULLETS)
        return;
    bullet = &play->bullets[play->bullet_count++];
    memset(bullet, 0, sizeof(*bullet));
    bullet->weapon = weapon;
    bullet->position = bullet->from = at;
    /* Turned to face ahead, keeping its whole speed. */
    bullet->velocity = vec3(0.0f, v.y, sqrtf(vec3_dot(v, v)));
    bullet->bounce = bullet->velocity;
}

/*
 * A click with a weapon (0x4055e0): once it is ready again, both guns or
 * both bazookas fire, or the cannon once. When the shots run out the bat
 * is plain again.
 */
static void bat_fire(Play *play)
{
    Bat *bat = &play->bat;
    Vec3 p = bat->position;
    Weapon weapon;
    const char *shot;

    switch (bat->mode) {
    case BAT_GUN:
        weapon = WEAPON_GUN;
        shot = "gunshoot.ogg";
        fire(play, weapon, vec3(p.x + 2.35f, p.y + 0.55f, p.z));
        fire(play, weapon, vec3(p.x - 2.35f, p.y + 0.55f, p.z));
        bat->ammo -= 2;
        break;
    case BAT_BAZOOKA:
        weapon = WEAPON_BAZOOKA;
        shot = "bazukashoot.ogg";
        fire(play, weapon, vec3(p.x + 2.2f, p.y + 0.5f, p.z + 1.0f));
        fire(play, weapon, vec3(p.x - 2.2f, p.y + 0.5f, p.z + 1.0f));
        bat->ammo -= 2;
        break;
    case BAT_CANNON:
        weapon = WEAPON_CANNON;
        shot = "cannonshoot.ogg";
        fire(play, weapon, vec3(p.x, p.y + 2.0f, p.z + 1.0f));
        bat->ammo--;
        break;
    default:
        return;
    }
    bat->weapon_time = 251; /* the firing animation */
    bat->ready_time = play->time + (uint32_t)play->bullet_info[weapon].delay;
    if (bat->ammo <= 0) {
        bat->mode = BAT_NORMAL;
        bat->weapon_time = 0;
    }
    bat_sound(play, shot);
}

/*
 * A shot's step (0x409050): it flies, bouncing on the floor like a prize,
 * for 5 s. Rockets and cannon balls leave a red trail every 20 ms. A
 * bullet hits the brick it reaches, a rocket breaks it and flies on, and
 * a cannon ball blows it up. Returns false when it is gone.
 */
static bool bullet_update(Play *play, Bullet *bullet, uint32_t ms, float dt)
{
    const BulletInfo *info = &play->bullet_info[bullet->weapon];
    int index;

    bullet->age += ms;
    bullet->spin += (float)ms * 0.01f;
    if (bullet->age > 5000)
        return false;
    bullet->position = vec3_add(bullet->position, vec3_scale(bullet->velocity, dt));
    if (bullet->position.y <= -0.75f) {
        bullet->position.y = -0.74f;
        if (bullet->bounce.y >= 0.05f) {
            bullet->bounce = vec3_scale(bullet->bounce, 0.75f);
            bullet->velocity = bullet->bounce;
        } else {
            bullet->velocity.y = 0.0f;
        }
    } else {
        bullet->velocity.y -= dt * info->gravity;
    }

    if (bullet->age > bullet->trail_time && play->trail_count < PLAY_MAX_TRAIL &&
        (info->kind == BULLET_ROCKET || info->kind == BULLET_YADRO)) {
        TrailSpot *spot = &play->trail[play->trail_count++];
        spot->position = bullet->position;
        spot->color = 0xffff0000u;
        spot->size = 0.75f;
        spot->life = 250;
        if (info->kind == BULLET_YADRO) {
            float y = random_range(-0.1f, 0.2f);
            spot->position.x += random_range(-0.1f, 0.2f);
            spot->position.y += y;
            spot->size = random_range(0.8f, 0.4f);
        }
        bullet->trail_time = bullet->age + 20;
    }

    index = brick_at(play, bullet->position);
    if (index < 0) {
        /* Fire and fountains stop bullets and cannon balls, not rockets. */
        index = billboard_at(play, bullet->position);
        if (index < 0 || info->kind == BULLET_ROCKET)
            return true;
        if (info->kind != BULLET_BULLET)
            explode(play, play->scene->billboards[index].position);
        return false;
    }
    switch (info->kind) {
    case BULLET_ROCKET:
        brick_destroy(play, index);
        return true;
    case BULLET_YADRO:
    case BULLET_SMALL_CANNON:
        explode(play, play->scene->bricks[index].position);
        return false;
    default:
        brick_hit(play, index, NULL);
        return false;
    }
}

/* The board's walls and the bat, after everything has moved (0x406b80). */
static void ball_vs_board(Play *play, Ball *ball)
{
    const Board *board = &play->scene->board;
    Vec3 v = ball->velocity;
    Vec3 p = ball->position;

    if (ball->flags & BALL_STUCK)
        return;

    switch (board->shape) {
    case BOARD_RECT:
        if (p.x > board->half_width) {
            v.x = -fabsf(v.x);
            wall_sound(play, ball);
        } else if (p.x < -board->half_width) {
            v.x = fabsf(v.x);
            wall_sound(play, ball);
        } else if (p.z > board->half_height) {
            v.z = -fabsf(v.z);
            wall_sound(play, ball);
        }
        ball_set_velocity(ball, v);
        break;
    case BOARD_ROUND:
        if (round_wall(ball, vec3(0.0f, 0.0f, board->scale_z * 7.5f), board->radius))
            wall_sound(play, ball);
        break;
    case BOARD_COMPOUND:
        if (p.z < board->half_height + 0.25f) {
            if (p.x > board->half_width) {
                v.x = -fabsf(v.x);
                wall_sound(play, ball);
            } else if (p.x < -board->half_width) {
                v.x = fabsf(v.x);
                wall_sound(play, ball);
            }
            ball_set_velocity(ball, v);
        } else {
            Vec3 centre = vec3(0.0f, 0.0f, board->scale_z * 12.5f);
            if (round_wall(ball, centre, board->radius)) {
                /* Pulled towards the middle so it can't get stuck bouncing. */
                if (ball->round_bounces > 3) {
                    ball->position.x -= (ball->position.x - centre.x) * 0.05f;
                    ball->position.z -= (ball->position.z - centre.z) * 0.05f;
                }
                ball->round_bounces++;
                wall_sound(play, ball);
            }
        }
        break;
    }

    if (ball->position.z < board->bat_line + BAT_REACH) {
        if (!ball_vs_bat(play, ball) && ball->position.z <= board->bat_line)
            ball->flags = BALL_LOST;
    } else {
        ball->round_bounces = 0;
    }
}

/* ------------------------------------------------------------------------ */

static void new_ball(Play *play)
{
    play->bat.position.x = 0.0f;
    play->bat.half_width = play->bat.target_width = BAT_HALF_WIDTH;
    play->bat.mode = BAT_NORMAL;
    play->bat.ammo = 0;
    play->bat.tilt = 0;
    play->bat.bounce_time = 0;
    play->bat.bounce_kind = 0;
    play->bat.pieces_ms = 0;
    play->ball_count = 0;
    ball_add(play, vec3(play->bat.position.x, 0.0f, play->bat.position.z + 0.5f), true);
}

/* Where everything is before a step, for drawing between steps. */
static void remember_positions(Play *play)
{
    int i;

    for (i = 0; i < play->ball_count; i++) {
        play->balls[i].from = play->balls[i].position;
        play->balls[i].spin_from = play->balls[i].spin;
    }
    for (i = 0; i < play->prize_count; i++)
        play->prizes[i].from = play->prizes[i].position;
    for (i = 0; i < play->bullet_count; i++)
        play->bullets[i].from = play->bullets[i].position;
    for (i = 0; i < BAT_PIECES; i++)
        play->bat.pieces[i].from = play->bat.pieces[i].position;
    for (i = 0; i < play->particle_count; i++)
        play->particles[i].from = play->particles[i].position;
    for (i = 0; i < play->smoke_count; i++)
        play->smoke[i].from = play->smoke[i].position;
    for (i = 0; i < play->spark_count; i++)
        play->sparks[i].from = play->sparks[i].position;
    for (i = 0; i < play->firework_count; i++)
        play->fireworks[i].from = play->fireworks[i].position;
    for (i = 0; i < play->scene->brick_count; i++) {
        const SceneBrick *b = &play->scene->bricks[i];
        PlayBrick *pb = &play->bricks[i];
        pb->from = b->position;
        pb->tip_from = b->tip;
        pb->roll_from = b->roll;
        pb->scale_from = b->scale;
    }
}

bool play_start(Play *play, Scene *scene, int score, int lives, float mouse_sensitivity)
{
    int i;

    memset(play, 0, sizeof(*play));
    play->scene = scene;
    play->score = score;
    play->lives = lives;
    play->mouse_sensitivity = mouse_sensitivity;
    play->bricks = calloc((size_t)(scene->brick_count + 1), sizeof(*play->bricks));
    if (play->bricks == NULL)
        return false;

    for (i = 0; i < scene->brick_count; i++) {
        PlayBrick *pb = &play->bricks[i];
        pb->hits_left = 1;
        pb->counts = true;
        switch (scene->bricks[i].object->type) {
        case 0x10: pb->points = 10; break;
        case 0x18: pb->points = 7; pb->counts = false; pb->hits_left = 2; break;
        case 0x20: pb->points = 8; break;
        case 0x28: pb->points = 0; pb->hits_left = 4; break;
        case 0x30: pb->points = 12; pb->counts = false; pb->hits_left = 6; break;
        default: pb->points = 5; break;
        }
        if (scene->bricks[i].animal != NULL) {
            pb->points = 22;
            pb->idle_ms = (uint32_t)(rand() % 5000);
            pb->call_ms = (uint32_t)(rand() % 15000);
            if (scene->bricks[i].animal->kind == MODEL_TELEPORT || scene->bricks[i].animal->kind == MODEL_TELEPORT_END)
                pb->counts = false;
        }
        if (pb->counts)
            play->bricks_left++;
        {
            Cell c = cell_of(scene->bricks[i].position);
            pb->cell_x = c.x;
            pb->cell_y = c.y;
            pb->cell_z = c.z;
            pb->target_x = scene->bricks[i].position.x;
            pb->target_y = scene->bricks[i].position.y;
        }
        grid_place(play, i);
    }

    play->bat.position = vec3(0.0f, 0.0f, scene->board.bat_line);
    new_ball(play);
    load_prize_info(play);
    load_weapons(play);
    remember_positions(play);
    return true;
}

void play_free(Play *play)
{
    free(play->bricks);
    memset(play, 0, sizeof(*play));
}

/* The bat follows the mouse (0x4042f0); the model leans as it goes. */
void play_mouse_move(Play *play, int dx)
{
    Bat *bat = &play->bat;
    float limit = play->scene->board.half_width - bat->half_width;
    float move;

    if (play->state != PLAY_RUNNING)
        return;
    dx = (int)((float)dx * play->mouse_sensitivity);
    move = (float)dx * 0.18f;
    bat->position.x += move;
    bat->tilt = (int)((float)bat->tilt - move * 70.0f);
    if (bat->tilt > 299) bat->tilt = 299;
    if (bat->tilt < -299) bat->tilt = -299;
    if (bat->position.x > limit) bat->position.x = limit;
    if (bat->position.x < -limit) bat->position.x = -limit;
}

void play_click(Play *play)
{
    if (play->state != PLAY_RUNNING || play->bat.mode == BAT_DEAD ||
        play->time < play->bat.ready_time)
        return;
    release_balls(play);
    bat_fire(play);
}

void play_hold_trigger(Play *play, bool held)
{
    play->trigger = held;
}

static void step(Play *play, uint32_t ms)
{
    float dt = (float)ms * 0.01f;
    int i, kept;

    play->time += ms;
    play->state_time += ms;

    /* The bat eases back upright. */
    play->bat.tilt += play->bat.tilt > 0 ? -3 : 3;
    bat_pieces_step(&play->bat, ms);
    /* Its bounce plays at a third of real time, then stops (0x404f4f). */
    if (play->bat.bounce_time != 0) {
        static const unsigned end[3] = { 150, 1150, 1300 };
        static const unsigned start[3] = { 1, 1000, 1150 };
        play->bat.bounce_ms += ms; /* kept apart so short frames still count */
        play->bat.bounce_time = start[play->bat.bounce_kind] + play->bat.bounce_ms / 3;
        if (play->bat.bounce_time > end[play->bat.bounce_kind])
            play->bat.bounce_time = 0;
    }

    /* The bat grows or shrinks to its new width at 0.1 a step (0x404ffc). */
    if (play->bat.half_width < play->bat.target_width) {
        play->bat.half_width += 0.1f * dt;
        if (play->bat.half_width > play->bat.target_width)
            play->bat.half_width = play->bat.target_width;
    } else if (play->bat.half_width > play->bat.target_width) {
        play->bat.half_width -= 0.1f * dt;
        if (play->bat.half_width < play->bat.target_width)
            play->bat.half_width = play->bat.target_width;
    }
    /* The weapons come out over 750 ms, and each shot plays 251-500 (0x404fb6). */
    if (play->bat.weapon_time < 250) {
        play->bat.weapon_time += ms / 3;
        if (play->bat.weapon_time > 250)
            play->bat.weapon_time = 250;
    } else if (play->bat.weapon_time > 250) {
        play->bat.weapon_time += ms / 3;
        if (play->bat.weapon_time >= 500)
            play->bat.weapon_time = 250;
    }
    /* The game reads the button every frame, so holding it keeps firing (0x419875). */
    if (play->trigger && play->state == PLAY_RUNNING && play->time >= play->bat.ready_time)
        bat_fire(play);
    if (play->bat.mode == BAT_RUBBER && play->time > play->bat.rubber_end) {
        release_yoyo(play);
        play->bat.mode = BAT_NORMAL;
        play->bat.weapon_time = 0;
    }
    /* Hypnotic balls turn plain again, explosive ones too (0x40e790). */
    if (play->hypnotic_end != 0 && play->time > play->hypnotic_end) {
        for (i = 0; i < play->ball_count; i++)
            play->balls[i].flags &= ~(uint32_t)(BALL_HYPNOTIC | BALL_EXPLOSIVE);
        play->hypnotic_end = 0;
    }
    for (i = 0, kept = 0; i < play->trail_count; i++) {
        play->trail[i].life -= (int)ms;
        if (play->trail[i].life >= 0)
            play->trail[kept++] = play->trail[i];
    }
    play->trail_count = kept;

    if (scene_update_effects(play->scene, ms))
        play->bricks_changed = true;
    for (i = 0; i < play->scene->brick_count; i++) {
        brick_gravity(play, i);
        brick_move(play, i, ms);
        if (play->scene->bricks[i].animal != NULL && play->scene->bricks[i].animal->kind == MODEL_ANIMAL)
            animal_update(play, i, ms, dt);
        else if (play->scene->bricks[i].object->type == 0x38 && !play->scene->bricks[i].removed)
            fog_update(play, i, ms);
    }

    /* Puffs fly, fall 0.05 a step, shrink 0.01 and bounce off the floor (0x41f3f0). */
    for (i = 0, kept = 0; i < play->particle_count; i++) {
        Particle *p = &play->particles[i];
        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
        p->velocity.y -= 0.05f * dt;
        p->size -= 0.01f * dt;
        if (p->position.y < -0.75f)
            p->velocity.y = fabsf(p->velocity.y) * 0.75f;
        if (p->size >= 0.25f)
            play->particles[kept++] = *p;
    }
    play->particle_count = kept;

    /* Smoke drifts, rises ever faster and grows, fading in over its first
       second and out over its last (0x4243a0). */
    for (i = 0, kept = 0; i < play->smoke_count; i++) {
        Particle *p = &play->smoke[i];
        int alpha;
        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
        p->velocity.y += 0.05f * dt;
        p->size += 0.05f * dt;
        p->life -= (int)ms;
        if (p->life < 0)
            continue;
        alpha = p->life < 1000 ? p->life / 4 : 250 - (p->life - 1000) / 4;
        p->color = (p->color & 0xffffffu) | (uint32_t)(alpha & 0xff) << 24;
        play->smoke[kept++] = *p;
    }
    play->smoke_count = kept;

    for (i = 0, kept = 0; i < play->bolt_count; i++)
        if (bolt_update(&play->bolts[i], ms))
            play->bolts[kept++] = play->bolts[i];
    play->bolt_count = kept;

    for (i = 0, kept = 0; i < play->firework_count; i++)
        if (firework_update(play, &play->fireworks[i], ms, dt))
            play->fireworks[kept++] = play->fireworks[i];
    play->firework_count = kept;

    /* Sparks fly out, slow, fall 0.025 a step and shrink 0.02, bouncing off
       the floor, and fade over their last half second (0x40c140). */
    for (i = 0, kept = 0; i < play->spark_count; i++) {
        Particle *p = &play->sparks[i];
        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
        p->velocity.y -= 0.025f * dt;
        p->velocity = vec3_scale(p->velocity, 1.0f - 0.1f * dt);
        p->size -= 0.02f * dt;
        p->life -= (int)ms;
        if (p->size <= 0.25f || p->life < 0)
            continue;
        if (p->life < 500)
            p->color = (p->color & 0xffffffu) | (uint32_t)(p->life / 2) << 24;
        if (p->position.y < -0.75f)
            p->velocity.y = fabsf(p->velocity.y) * 0.75f;
        play->sparks[kept++] = *p;
    }
    play->spark_count = kept;

    for (i = 0; i < play->ball_count; i++)
        ball_update(play, &play->balls[i], ms, dt);
    for (i = 0; i < play->ball_count; i++)
        ball_vs_board(play, &play->balls[i]);

    for (i = 0, kept = 0; i < play->ball_count; i++) {
        if (!(play->balls[i].flags & BALL_LOST))
            play->balls[kept++] = play->balls[i];
    }
    play->ball_count = kept;

    for (i = 0, kept = 0; i < play->bullet_count; i++) {
        if (bullet_update(play, &play->bullets[i], ms, dt))
            play->bullets[kept++] = play->bullets[i];
    }
    play->bullet_count = kept;

    for (i = 0, kept = 0; i < play->prize_count; i++) {
        if (prize_update(play, &play->prizes[i], ms, dt))
            play->prizes[kept++] = play->prizes[i];
    }
    play->prize_count = kept;

    switch (play->state) {
    case PLAY_RUNNING:
        /* With five bricks or fewer to go, a next-level medal comes out
           of the middle now and then (0x4197fa). */
        if (play->bricks_left <= 5) {
            play->medal_timer -= (int)ms;
            if (play->medal_timer < 0) {
                prize_roll(play, vec3(0.0f, 0.0f, 10.0f), PRIZE_NEXTLEVEL);
                play->medal_timer = 9000 + rand() % 5000;
            }
        }
        if (play->bricks_left <= 0) {
            play->state = PLAY_LEVEL_DONE;
            play->state_time = 0;
        } else if (play->ball_count == 0) {
            bat_kill(play);
            play->state = PLAY_BAT_LOST;
            play->state_time = 0;
        }
        break;
    case PLAY_BAT_LOST:
        if (play->state_time >= BAT_LOST_MS) {
            if (--play->lives < 0) {
                play->state = PLAY_GAME_OVER;
            } else {
                new_ball(play);
                play->state = PLAY_RUNNING;
            }
            play->state_time = 0;
        }
        break;
    default:
        break;
    }
}

float play_blend(const Play *play)
{
    return (float)play->pending_ms / (float)STEP_MS;
}

void play_update(Play *play, uint32_t ms)
{
    play->pending_ms += ms;
    /* After a long stall, don't try to catch up all at once. */
    if (play->pending_ms > 10 * STEP_MS)
        play->pending_ms = 10 * STEP_MS;
    while (play->pending_ms >= STEP_MS) {
        remember_positions(play);
        step(play, STEP_MS);
        play->pending_ms -= STEP_MS;
    }
}

void play_autopilot(Play *play)
{
    const Ball *ball;
    float limit = play->scene->board.half_width - play->bat.half_width;

    if (play->state != PLAY_RUNNING || play->ball_count == 0)
        return;
    ball = &play->balls[0];
    if (ball->flags & BALL_STUCK) {
        if (play->state_time > 500 || play->time > 500)
            play_click(play);
        return;
    }
    /* Off centre by an amount that changes now and then, so the ball goes everywhere. */
    play->bat.position.x = ball->position.x - (float)((int)(play->time / 1700) % 5 - 2) * 1.1f;
    if (play->bat.position.x > limit) play->bat.position.x = limit;
    if (play->bat.position.x < -limit) play->bat.position.x = -limit;
}
