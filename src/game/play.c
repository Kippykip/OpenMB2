/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "play.h"
#include "../core/log.h"

#include <math.h>
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

/* Bricks sound quieter further away, and are panned by x (0x403660). */
static void brick_sound(Play *play, int index, const char *name)
{
    Vec3 p = play->scene->bricks[index].position;
    int volume = (int)(100.0f - fabsf(p.z + p.z));
    if (volume >= 0)
        sound(play, name, volume, (int)(p.x / play->scene->board.half_width * 100.0f));
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
static void brick_take(Play *play, int index)
{
    SceneBrick *b = &play->scene->bricks[index];
    PlayBrick *pb = &play->bricks[index];

    if (b->removed)
        return;
    b->removed = true;
    grid_remove(play, index);
    scene_break_brick(play->scene, index);
    if (pb->counts)
        play->bricks_left--;
    play->score += pb->points * 100;
    play->bricks_changed = true;
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
    Vec3 p;

    if (self < 0)
        return;
    p = play->scene->bricks[self].position;
    brick_take(play, self);
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
static void ball_vs_brick(Play *play, Ball *ball)
{
    int index = brick_at(play, ball->position);
    const SceneBrick *b;
    Vec3 p, prev, v;

    if (index < 0)
        return;
    b = &play->scene->bricks[index];
    p = ball->position;
    prev = ball->previous;
    v = ball->velocity;

    if (p.z < b->position.z + 1.0f && prev.z > b->position.z + 1.0f)
        v.z = fabsf(v.z);
    else if (p.z > b->position.z - 1.0f && prev.z < b->position.z - 1.0f)
        v.z = -fabsf(v.z);
    else if (p.x > b->position.x - 0.99f && prev.x < b->position.x - 0.99f)
        v.x = -fabsf(v.x);
    else if (p.x < b->position.x + 1.01f && prev.x > b->position.x + 1.01f)
        v.x = fabsf(v.x);
    else
        return;

    /* Only the bottom layer is in the ball's way. */
    if (b->position.y > 0.5f)
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
    ball->position = ball->previous = at;
    ball->velocity = vec3(-0.35f * s, 0.0f, 0.65f * s);
    if (stuck)
        ball_stick(play, ball, 0.0f);
    return ball;
}

/* One step of a ball's own movement (0x402060). */
static void ball_update(Play *play, Ball *ball, float dt)
{
    ball->previous = ball->position;
    if (ball->flags & BALL_STUCK) {
        ball->position.x = play->bat.position.x + ball->stuck_offset;
        ball->position.y = BALL_Y;
        return;
    }
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

/* A ball against the bat; true if it bounced (0x405450). */
static bool ball_vs_bat(Play *play, Ball *ball)
{
    Bat *bat = &play->bat;
    float d;

    if (play->state != PLAY_RUNNING)
        return false;
    if (ball->position.x < bat->position.x - bat->half_width ||
        ball->position.x >= bat->position.x + bat->half_width)
        return false;
    d = ball->position.x - bat->position.x;
    ball_set_angle(ball, BAT_ANGLE / bat->half_width * d);
    ball_speed_up(play, ball, 0.01f);
    sound(play, "ball2.ogg", -1, 0);
    return true;
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
    play->bat.half_width = BAT_HALF_WIDTH;
    play->bat.tilt = 0;
    play->ball_count = 0;
    ball_add(play, vec3(play->bat.position.x, 0.0f, play->bat.position.z + 0.5f), true);
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
    int i;
    if (play->state != PLAY_RUNNING)
        return;
    for (i = 0; i < play->ball_count; i++)
        play->balls[i].flags &= ~(uint32_t)BALL_STUCK;
}

static void step(Play *play, uint32_t ms)
{
    float dt = (float)ms * 0.01f;
    int i, kept;

    play->time += ms;
    play->state_time += ms;

    /* The bat eases back upright. */
    play->bat.tilt += play->bat.tilt > 0 ? -3 : 3;

    if (scene_update_effects(play->scene, ms))
        play->bricks_changed = true;
    for (i = 0; i < play->scene->brick_count; i++) {
        brick_gravity(play, i);
        brick_move(play, i, ms);
    }

    for (i = 0; i < play->ball_count; i++)
        ball_update(play, &play->balls[i], dt);
    for (i = 0; i < play->ball_count; i++)
        ball_vs_board(play, &play->balls[i]);

    for (i = 0, kept = 0; i < play->ball_count; i++) {
        if (!(play->balls[i].flags & BALL_LOST))
            play->balls[kept++] = play->balls[i];
    }
    play->ball_count = kept;

    switch (play->state) {
    case PLAY_RUNNING:
        if (play->bricks_left <= 0) {
            play->state = PLAY_LEVEL_DONE;
            play->state_time = 0;
        } else if (play->ball_count == 0) {
            sound(play, "batkilled.ogg", -1, 0);
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

void play_update(Play *play, uint32_t ms)
{
    play->pending_ms += ms;
    /* After a long stall, don't try to catch up all at once. */
    if (play->pending_ms > 10 * STEP_MS)
        play->pending_ms = 10 * STEP_MS;
    while (play->pending_ms >= STEP_MS) {
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
