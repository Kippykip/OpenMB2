/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "menu_screen.h"
#include "assets.h"
#include "intro.h"
#include "play_screen.h"
#include "userfile.h"
#include "audio/audio.h"
#include "core/log.h"
#include "core/matrix.h"
#include "game/font.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/play.h"
#include "game/save.h"
#include "game/scores.h"
#include "game/textures.h"
#include "render/render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The original's game states (0x418b70), as far as the menus go: each
 * page is a list of items, and the high scores and the name page draw
 * a little more over theirs.
 */
typedef enum Page {
    PAGE_SPLASH,
    PAGE_INTRO,         /* 0x1000 */
    PAGE_MENU,          /* 0x40, 0x80, 0x10000: just the list */
    PAGE_SCORES,        /* 0x200 */
    PAGE_GLOBAL_SCORES, /* 0x40000: the online table, which is gone */
    PAGE_ENTER_NAME     /* 0x400 */
} Page;

#define NAME_LENGTH 12     /* 0x41bf7c */
#define CLICK_DELAY 500    /* ms an item must be showing before it takes a click */
#define VIDEO_MODES 6

static const char *const k_video_modes[VIDEO_MODES] = {
    "640 * 480", "800 * 600", "1024 * 768", "1152 * 864", "1280 * 960", "1600 * 1200"
};

/* The running line at the bottom (0x4163c0): seven lines from the
   language file, faded in and out or run across, then round again. */
static const struct {
    bool runs;
    uint32_t color, start, end;
} k_ticker[7] = {
    { false, 0xffffff00u, 3000, 8000 },     { false, 0xff0000ffu, 9000, 14000 },
    { false, 0xffff0000u, 15000, 20000 },   { false, 0xff00ff00u, 21000, 26000 },
    { true, 0xffffff00u, 27000, 100000 },   { true, 0xff88ff88u, 100000, 200000 },
    { true, 0xffffffffu, 200000, 300000 },
};

/* What each item is doing, beside what its file says. */
typedef struct ItemState {
    uint32_t age;      /* ms since its page appeared, or since it was last clicked */
    bool hover;
    float handle;      /* a slider's knob, 0 to senswdth */
    unsigned texture;  /* its picture's GL texture, 0 for none */
    int order;         /* where its texture comes in the texture set */
    float cell[2];     /* the texture script's cell size */
} ItemState;

#define BATCH_VERTICES 24576
#define BATCH_PARTS 768

typedef struct BatchPart {
    RenderPart part;
    int order, sequence;
} BatchPart;

static struct {
    XVertex vertices[BATCH_VERTICES];
    int vertex_count;
    BatchPart parts[BATCH_PARTS];
    int part_count;
} batch;

static struct {
    SDL_Window *window;
    Params *params;
    bool intro_enabled;
    MenuAction action;
    Page page;
    uint32_t page_time;
    MenuList list;
    ItemState *states;
    int state_capacity;
    TextureSet textures;
    RenderLight light;
    Font text_font, service_font;
    unsigned font_texture, service_texture, cursor_texture, splash_texture;
    XModel menu_model;
    RenderMesh menu_mesh, mesh;
    float fon_time;
    uint32_t time;
    /* The running line. */
    uint32_t ticker_time;
    int ticker_line;
    float ticker_x;
    /* The mouse, in the 800x600 screen. */
    float mouse_x, mouse_y;
    bool button;        /* the left button, as the items see it */
    bool button_down;   /* the left button itself */
    /* A click that opens a page doesn't go on to click what is under it
       there: the button has to come up first. (The original lets it
       through, so opening the options could move a slider.) */
    bool wait_release;
    float view_width, view_height;
    /* Where on the screen the menus go, in pixels from the bottom left:
       4:3 between black bars, or (--wide-menus) all of it. */
    int viewport[4];
    int screen_width, screen_height;
    bool wide;
    /* The game to start. */
    char pack[64];
    char pad[MENU_PATH_SIZE];
    int level;
    int progress[SAVE_MAX_LEVELS][3];
    /* High scores and the name page. */
    Scores scores;
    int highlight;      /* the place just added, -1 for none */
    float scroll;
    int score;
    char name[NAME_LENGTH + 1];
    float pulse;
    int screw_sound;    /* the sound slider's noise while it's dragged */
    /* The help pages' prizes. */
    PrizeInfo prize_info[PRIZE_KINDS];
    XModel prize_models[PRIZE_KINDS];
    RenderMesh prize_meshes[PRIZE_KINDS];
    bool prize_tried[PRIZE_KINDS];
} m;

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

static void set_color(float *rgba, uint32_t argb)
{
    rgba[0] = (float)((argb >> 16) & 0xff) / 255.0f;
    rgba[1] = (float)((argb >> 8) & 0xff) / 255.0f;
    rgba[2] = (float)(argb & 0xff) / 255.0f;
    rgba[3] = (float)(argb >> 24) / 255.0f;
}

static void batch_begin(void)
{
    batch.vertex_count = 0;
    batch.part_count = 0;
}

static void batch_part(int order, unsigned texture, uint32_t color, int first, int count)
{
    BatchPart *p;

    if (count == 0 || batch.part_count == BATCH_PARTS)
        return;
    p = &batch.parts[batch.part_count];
    memset(p, 0, sizeof(*p));
    p->order = order;
    p->sequence = batch.part_count++;
    p->part.first_vertex = first;
    p->part.vertex_count = count;
    p->part.texture = texture;
    set_color(p->part.diffuse, color);
    p->part.flags = RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_NO_DEPTH_TEST | RENDER_TINT |
                    RENDER_TWO_SIDED | RENDER_NOT_LIGHTED;
    p->part.pose = -1;
}

/* A picture from (left, top) to (right, bottom), its texture's (u0, v0)
   at the top left. */
static void batch_quad(int order, unsigned texture, float left, float top, float right, float bottom,
                       float u0, float v0, float u1, float v1, uint32_t color)
{
    static const int corners[6] = { 0, 1, 2, 0, 2, 3 };
    float x[4] = { left, right, right, left }, y[4] = { top, top, bottom, bottom };
    float u[4] = { u0, u1, u1, u0 }, v[4] = { v0, v0, v1, v1 };
    int first = batch.vertex_count, k;

    if (texture == 0 || batch.vertex_count + 6 > BATCH_VERTICES)
        return;
    for (k = 0; k < 6; k++) {
        XVertex *q = &batch.vertices[batch.vertex_count++];
        memset(q, 0, sizeof(*q));
        q->pos[0] = x[corners[k]];
        q->pos[1] = y[corners[k]];
        q->normal[2] = -1.0f;
        q->uv[0] = u[corners[k]];
        q->uv[1] = v[corners[k]];
    }
    batch_part(order, texture, color, first, 6);
}

enum { ORDER_TEXT = 0, ORDER_SERVICE = 1, ORDER_CURSOR = 2 };

/*
 * Text on the text font (or the service font). clip > 0 keeps only the
 * letters within that distance of the middle, as the running line does,
 * and fades out those in the last 25 units, by where each letter's right
 * edge is (0x4275b2).
 */
static void batch_text_clipped(bool service, float x, float y, float scale, int align, const char *text,
                               uint32_t color, float clip)
{
    const Font *font = service ? &m.service_font : &m.text_font;
    unsigned texture = service ? m.service_texture : m.font_texture;
    int order = service ? ORDER_SERVICE : ORDER_TEXT;
    int first = batch.vertex_count, n, i, kept = 0, faded = 0;
    XVertex edge[6 * 8];
    uint32_t edge_alpha[8];

    if (text == NULL || text[0] == '\0')
        return;
    n = font_layout(font, x, y, scale, align, text, batch.vertices + first, BATCH_VERTICES - first);
    for (i = 0; i + 6 <= n; i += 6) {
        if (clip > 0.0f) {
            float right = batch.vertices[first + i].pos[0], d;
            int k;
            for (k = 1; k < 6; k++)
                right = fmaxf(right, batch.vertices[first + i + k].pos[0]);
            d = fabsf(right);
            if (d > clip)
                continue;
            if (d > clip - 25.0f) {
                if (faded < 8) {
                    memcpy(&edge[faded * 6], &batch.vertices[first + i], 6 * sizeof(XVertex));
                    edge_alpha[faded++] = (uint32_t)((clip - d) * 10.0f);
                }
                continue;
            }
        }
        if (kept != i)
            memmove(&batch.vertices[first + kept], &batch.vertices[first + i], 6 * sizeof(XVertex));
        kept += 6;
    }
    batch.vertex_count += kept;
    batch_part(order, texture, color, first, kept);
    for (i = 0; i < faded && batch.vertex_count + 6 <= BATCH_VERTICES; i++) {
        first = batch.vertex_count;
        memcpy(&batch.vertices[first], &edge[i * 6], 6 * sizeof(XVertex));
        batch.vertex_count += 6;
        batch_part(order, texture, edge_alpha[i] << 24 | (color & 0xffffffu), first, 6);
    }
}

static void batch_text(float x, float y, float scale, int align, const char *text, uint32_t color)
{
    batch_text_clipped(false, x, y, scale, align, text, color, 0.0f);
}

/*
 * By texture; the pictures of one texture last added first, as the
 * original's sprite lists go (the options page's dark panels go under
 * the rows listed before them), and words in the order written.
 */
static int compare_parts(const void *a, const void *b)
{
    const BatchPart *x = a, *y = b;
    if (x->order != y->order)
        return x->order - y->order;
    return x->order >= 1000 ? y->sequence - x->sequence : x->sequence - y->sequence;
}

/* Draw what's been batched, by texture as the original's sprite lists go. */
static void batch_draw(void)
{
    int i;

    render_mesh_destroy(&m.mesh);
    if (batch.vertex_count == 0 || !render_mesh_create(&m.mesh, batch.vertices, batch.vertex_count))
        return;
    qsort(batch.parts, (size_t)batch.part_count, sizeof(batch.parts[0]), compare_parts);
    for (i = 0; i < batch.part_count; i++)
        render_mesh_add_part(&m.mesh, &batch.parts[i].part);
    render_draw(&m.mesh, mat4_identity(), RENDER_PASS_ALL, 0.0f);
    batch_begin();
}

/* ------------------------------------------------------------------------ */
/* Items                                                                    */
/* ------------------------------------------------------------------------ */

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash))
        slash = back;
    return slash != NULL ? slash + 1 : path;
}

/* An item's picture: its texture, and where it comes in the texture set's
   order (0x4115d0 draws the sprites texture by texture). */
static void find_texture(const MenuItem *item, ItemState *state)
{
    static int unregistered = 0;
    const char *name = base_name(item->texture);
    int i;

    state->texture = 0;
    state->cell[0] = state->cell[1] = 1.0f;
    state->order = 100000 + unregistered++;
    if (SDL_strcasecmp(item->texture, "none") == 0 || SDL_strcasecmp(name, "fake.bmp") == 0)
        return;
    for (i = 0; i < m.textures.count; i++) {
        const TextureScript *script = &m.textures.scripts[i];
        if (SDL_strcasecmp(base_name(script->image), name) == 0) {
            state->order = 1000 + i;
            if (script->xfactor > 0.0f)
                state->cell[0] = script->xfactor;
            if (script->yfactor > 0.0f)
                state->cell[1] = script->yfactor;
            break;
        }
    }
    state->texture = asset_texture(item->texture);
}

static float slider_value(const MenuItem *item)
{
    if (strcmp(item->id, "sound") == 0)
        return (float)m.params->sound_volume;
    if (strcmp(item->id, "music") == 0)
        return (float)m.params->music_volume;
    if (strcmp(item->id, "msensivity") == 0)
        return (m.params->mouse_sensitivity - 0.5f) / 0.005f;
    return 0.0f;
}

static void prepare_items(void)
{
    int i;

    if (m.list.count > m.state_capacity) {
        ItemState *grown = realloc(m.states, (size_t)m.list.count * sizeof(*grown));
        if (grown == NULL) {
            m.list.count = m.state_capacity;
            return;
        }
        m.states = grown;
        m.state_capacity = m.list.count;
    }
    for (i = 0; i < m.list.count; i++) {
        MenuItem *item = &m.list.items[i];
        ItemState *state = &m.states[i];

        memset(state, 0, sizeof(*state));
        find_texture(item, state);
        if (item->kind == MENU_SLIDER)
            state->handle = slider_value(item) * item->sens_width / 100.0f;
        if (item->kind == MENU_BUTTON && strcmp(item->id, "selectpad") == 0)
            item->texture_x = SDL_strcasecmp(item->script, m.pad) == 0 ? 1.0f : 0.0f;
    }
}

static void stop_screw(void)
{
    if (m.screw_sound != 0)
        audio_stop_loop(m.screw_sound);
    m.screw_sound = 0;
}

/* A page from its list file, with the logos (0x418670, 0x418b70). */
static void load_page(const char *list, Page page)
{
    stop_screw();
    menu_list_clear(&m.list);
    if (list != NULL)
        menu_list_load(&m.list, list);
    menu_list_load(&m.list, "logos.txt");
    prepare_items();
    m.page = page;
    m.page_time = 0;
    m.wait_release = true;
    SDL_StopTextInput();
}

/* 0x80: a page a button names; the language may have changed. */
static void navigate(const char *list)
{
    lang_load(m.params->language);
    load_page(list, PAGE_MENU);
}

static void show_main(void)
{
    intro_stop();
    lang_load(m.params->language);
    load_page("Menu/lmainmenu.txt", PAGE_MENU);
    audio_play_music("Music/menu.txt");
}

/* 0x10000: a button for each pack, Menu/pack.txt placed by how many there are. */
static void show_packs(void)
{
    int packs = play_screen_pack_count(), n = packs + 1, i;
    float dy = 30.0f, offset = 10.0f;
    MenuItem *item;

    lang_load(m.params->language);
    menu_list_clear(&m.list);
    menu_list_load(&m.list, "Menu/lselectlevel.txt");
    menu_list_load(&m.list, "logos.txt");
    if (n <= 2) {
        offset = -40.0f;
    } else if (n == 3) {
        dy = 80.0f;
        offset = -10.0f;
    } else if (n == 4) {
        dy = 60.0f;
    } else if (n == 5) {
        dy = 35.0f;
        offset = -10.0f;
    }
    for (i = 0; i < packs; i++) {
        item = menu_list_add(&m.list, "Menu/pack.txt");
        if (item == NULL)
            break;
        item->y = item->y - (float)i * dy + offset;
        if (n > 4) {
            if (i & 1) {
                item->x = 165.0f;
                item->y += dy;
            } else {
                item->x = i == n - 2 ? 0.0f : -165.0f;
            }
        }
        item->text_index = 0xa0 + i;
        snprintf(item->script, sizeof(item->script), "%s", play_screen_pack_folder(i));
    }
    item = menu_list_add(&m.list, "Menu/mainmenu.txt");
    if (item != NULL)
        item->y = -177.0f;
    prepare_items();
    m.page = PAGE_MENU;
    m.page_time = 0;
    stop_screw();
}

static void start_game(const char *pack, int level)
{
    snprintf(m.pack, sizeof(m.pack), "%s", pack);
    m.level = level;
    stop_screw();
    m.action = MENU_ACTION_PLAY;
}

static void read_progress(const char *pack, bool *found)
{
    char name[96];
    unsigned char *data;
    size_t size;

    memset(m.progress, 0, sizeof(m.progress));
    snprintf(name, sizeof(name), "s%s.sav", pack);
    data = userfile_read(name, &size);
    *found = data != NULL && save_decode_progress(data, size, m.progress);
    free(data);
    m.progress[0][0] = 1;
}

/* "rescan" (0x41a1a5): a pack's level pages, at the one with its last
   open level, or straight into its first level if it's never been played. */
static void open_pack(const char *pack)
{
    char list[MENU_PATH_SIZE];
    bool found;
    int open = 0, i, page;

    snprintf(m.pack, sizeof(m.pack), "%s", pack);
    read_progress(pack, &found);
    if (!found) {
        start_game(pack, 0);
        return;
    }
    for (i = 0; i < SAVE_MAX_LEVELS; i++) {
        if (m.progress[i][0] != 0)
            open++;
    }
    page = open < 21 ? 1 : open < 41 ? 2 : open < 61 ? 3 : open < 81 ? 4 : 5;
    snprintf(list, sizeof(list), "%s/Menu/llevelsel%d.txt", pack, page);
    navigate(list);
}

static void save_scores(void)
{
    size_t size;
    unsigned char *data = scores_encode(&m.scores, &size);
    if (data != NULL)
        userfile_write("score.dat", data, size);
    free(data);
}

static void show_scores(int highlight)
{
    m.highlight = highlight;
    load_page("Menu/lonlhighscores.txt", PAGE_SCORES);
    m.scroll = 66.0f;
}

/* Enter, or the button on the name page: the name goes in the table. */
static void enter_name(void)
{
    int place = scores_add(&m.scores, m.name[0] != '\0' ? m.name : "Unnamed", m.score);
    if (place >= 0)
        save_scores();
    show_scores(place);
    audio_play_music("Music/menu.txt");
}

static void show_enter_name(void)
{
    load_page("Menu/lentername.txt", PAGE_ENTER_NAME);
    audio_play_music("Music/enter.txt");
    SDL_StartTextInput();
}

/* What clicking a button does (0x41a1a0). */
static void run_command(MenuItem *item)
{
    const char *id = item->id;
    int i;

    if (strcmp(id, "navigate") == 0) {
        navigate(item->script);
    } else if (strcmp(id, "startgame") == 0) {
        if (play_screen_pack_count() > 0)
            show_packs();
    } else if (strcmp(id, "rescan") == 0) {
        open_pack(item->script);
    } else if (strcmp(id, "selectpad") == 0) {
        snprintf(m.pad, sizeof(m.pad), "%s", item->script);
        for (i = 0; i < m.list.count; i++) {
            MenuItem *other = &m.list.items[i];
            if (other->kind == MENU_BUTTON && strcmp(other->id, "selectpad") == 0)
                other->texture_x = SDL_strcasecmp(other->script, m.pad) == 0 ? 1.0f : 0.0f;
        }
    } else if (strcmp(id, "entername") == 0) {
        show_enter_name();
    } else if (strcmp(id, "highscores") == 0) {
        show_scores(-1);
    } else if (strcmp(id, "viewglobalscore") == 0) {
        m.highlight = -1;
        load_page("Menu/lglobalhighscores.txt", PAGE_GLOBAL_SCORES);
        m.scroll = -200.0f;
    } else if (strcmp(id, "navigatefromenterscore") == 0 || strcmp(id, "mysendscore") == 0) {
        enter_name();
    } else if (strcmp(id, "clearscore") == 0) {
        if (scores_count(&m.scores) > 0) {
            memset(&m.scores, 0, sizeof(m.scores));
            save_scores();
        }
    } else if (strcmp(id, "exit") == 0 || strcmp(id, "realexit") == 0) {
        m.action = MENU_ACTION_QUIT;
    } else {
        log_info("menu: '%s' does nothing here", id);
    }
}

static bool inside(float x, float y, float left, float right, float bottom, float top)
{
    return x >= left && x <= right && y >= bottom && y <= top;
}

static void click_sound(void)
{
    audio_play("Click.ogg", -1, 0);
}

/* 0x41e020: Select.ogg as the pointer comes onto it; a click once it
   has been showing for half a second. Returns true when clicked. */
static bool update_button(MenuItem *item, ItemState *state)
{
    float dy = item->height - 8.0f;

    if (!inside(m.mouse_x, m.mouse_y, item->x - item->width, item->x + item->width, item->y - dy,
                item->y + dy)) {
        state->hover = false;
        return false;
    }
    if (!state->hover)
        audio_play("Select.ogg", -1, 0);
    state->hover = true;
    if (state->age < CLICK_DELAY || !m.button)
        return false;
    /* Nothing to clear, or the pad already chosen, clicks without a sound. */
    if (!(strcmp(item->id, "clearscore") == 0 && scores_count(&m.scores) == 0) &&
        !(strcmp(item->id, "selectpad") == 0 && SDL_strcasecmp(item->script, m.pad) == 0))
        click_sound();
    state->age = 0;
    return true;
}

static bool *checkbox_value(const MenuItem *item)
{
    if (strcmp(item->id, "joystick") == 0)
        return &m.params->joystick;
    if (strcmp(item->id, "infravision") == 0)
        return &m.params->infravision;
    return NULL;
}

static void apply_settings(void)
{
    audio_set_volumes(m.params->sound_volume, m.params->music_volume);
    play_screen_set_options(m.params->mouse_sensitivity, m.params->infravision);
}

static float box_left(const MenuItem *item)
{
    return item->x - (item->sens_width > item->width ? item->sens_width : item->width);
}

static float box_right(const MenuItem *item)
{
    return item->x - (item->sens_width > item->width ? item->width : item->sens_width);
}

static void update_checkbox(MenuItem *item, ItemState *state)
{
    bool *value = checkbox_value(item);

    state->hover = inside(m.mouse_x, m.mouse_y, box_left(item), box_right(item), item->y - item->height,
                          item->y + item->height);
    if (state->hover && m.button && state->age >= CLICK_DELAY && value != NULL) {
        *value = !*value;
        state->age = 0;
        click_sound();
        apply_settings();
    }
}

static int select_count(const MenuItem *item)
{
    return item->video_mode ? VIDEO_MODES : lang_count();
}

static int *select_value(const MenuItem *item)
{
    return item->video_mode ? &m.params->resolution : &m.params->language;
}

/* -1 over the left arrow, 1 over the right one, 0 elsewhere. */
static int select_side(const MenuItem *item)
{
    if (m.mouse_y < item->y - item->height || m.mouse_y > item->y + item->height)
        return 0;
    if (m.mouse_x >= item->x + item->sens_width && m.mouse_x <= item->x + item->sens_width + 48.0f)
        return 1;
    if (m.mouse_x <= item->x - item->sens_width && m.mouse_x >= item->x - item->sens_width - 48.0f)
        return -1;
    return 0;
}

static void update_select(MenuItem *item, ItemState *state)
{
    int side = select_side(item), *value = select_value(item), count = select_count(item);

    state->hover = side != 0;
    if (side == 0 || !m.button || state->age < CLICK_DELAY)
        return;
    state->age = 0; /* held down, it steps every half second */
    if (side > 0 && *value < count - 1)
        (*value)++;
    else if (side < 0 && *value > 0)
        (*value)--;
    else
        return;
    click_sound();
}

static void update_slider(MenuItem *item, ItemState *state)
{
    float dy = item->height - 8.0f, value;
    bool dragging = m.button && inside(m.mouse_x, m.mouse_y, item->x, item->x + item->sens_width,
                                       item->y - dy, item->y + dy);

    if (strcmp(item->id, "sound") == 0) {
        if (dragging && m.screw_sound == 0)
            m.screw_sound = audio_play_loop("screw.ogg", -1, 0);
        else if (!dragging && m.screw_sound != 0)
            stop_screw();
    }
    if (!dragging)
        return;
    state->handle = m.mouse_x - item->x;
    value = state->handle / item->sens_width * 100.0f;
    if (strcmp(item->id, "sound") == 0)
        m.params->sound_volume = (int)value;
    else if (strcmp(item->id, "music") == 0)
        m.params->music_volume = (int)value;
    else if (strcmp(item->id, "msensivity") == 0)
        m.params->mouse_sensitivity = value * 0.005f + 0.5f;
    apply_settings();
}

/* The 5 x 4 levels of a level page: the cell under the pointer, or -1. */
static int level_cell(const MenuItem *item, int *col, int *row)
{
    int c, r;
    (void)item;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 5; c++) {
            float x = (float)c * 110.0f - 220.0f, y = 118.0f - (float)r * 76.0f;
            if (inside(m.mouse_x, m.mouse_y, x - 48.0f, x + 48.0f, y - 36.0f, y + 36.0f)) {
                *col = c;
                *row = r;
                return r * 5 + c;
            }
        }
    }
    return -1;
}

static bool level_open(int level)
{
    return level == 0 || (level > 0 && level < SAVE_MAX_LEVELS && m.progress[level][0] != 0);
}

static bool update_level_select(MenuItem *item, ItemState *state)
{
    int col, row, cell = level_cell(item, &col, &row);

    if (cell < 0 || !level_open(item->level_index + cell) || state->age < CLICK_DELAY || !m.button)
        return false;
    click_sound();
    start_game(m.pack, item->level_index + cell);
    return true;
}

static void update_items(uint32_t ms)
{
    int i;

    for (i = 0; i < m.list.count; i++)
        m.states[i].age += ms;
    for (i = 0; i < m.list.count && m.action == MENU_ACTION_NONE; i++) {
        MenuItem *item = &m.list.items[i];
        ItemState *state = &m.states[i];

        switch (item->kind) {
        case MENU_BUTTON:
            if (update_button(item, state)) {
                run_command(item); /* which may change the page */
                return;
            }
            break;
        case MENU_CHECKBOX: update_checkbox(item, state); break;
        case MENU_SELECT: update_select(item, state); break;
        case MENU_SLIDER: update_slider(item, state); break;
        case MENU_LEVEL_SELECT:
            if (update_level_select(item, state))
                return;
            break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing the items                                                        */
/* ------------------------------------------------------------------------ */

static void draw_picture(const MenuItem *item, const ItemState *state)
{
    float u0, v0, u1, v1;

    if (state->texture == 0 || item->width <= 0.0f || item->height <= 0.0f)
        return;
    if (item->has_size) {
        u0 = item->offset[0];
        v0 = item->offset[1];
        u1 = u0 + item->size[0];
        v1 = v0 + item->size[1];
    } else {
        u0 = item->texture_x * state->cell[0];
        v0 = item->texture_y * state->cell[1];
        u1 = u0 + state->cell[0];
        v1 = v0 + state->cell[1];
    }
    if (item->flipped) {
        float t = u0;
        u0 = u1;
        u1 = t;
    }
    batch_quad(state->order, state->texture, item->x - item->width, item->y + item->height,
               item->x + item->width, item->y - item->height, u0, v0, u1, v1, item->color);
}

/* CMenuItemLevelSelect: each level's picture, from the pack's sheet of
   them (4 across, 5 down, a column at a time). */
static void draw_level_select(const MenuItem *item, const ItemState *state)
{
    int col, row;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 5; col++) {
            float x = (float)col * 110.0f - 220.0f, y = 118.0f - (float)row * 76.0f;
            float u = (float)row * 0.25f, v = (float)col * 0.1875f;

            batch_quad(state->order, state->texture, x - 48.0f, y + 36.0f, x + 48.0f, y - 36.0f, u, v,
                       u + 0.25f, v + 0.1875f, item->color);
        }
    }
}

/* Then from the service font, a cross over each level still shut and a
   frame round the one under the pointer. */
static void draw_level_marks(const MenuItem *item, const ItemState *state)
{
    int col, row, hover = level_cell(item, &col, &row);

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 5; col++) {
            float x = (float)col * 110.0f - 220.0f, y = 118.0f - (float)row * 76.0f;
            if (!level_open(item->level_index + row * 5 + col))
                batch_text_clipped(true, x, y, 1.0f, FONT_CENTER, "B", 0xffffffffu, 0.0f);
            else if (hover == row * 5 + col && state->age > CLICK_DELAY)
                batch_text_clipped(true, x, y, 1.0f, FONT_CENTER, "A", 0xffffffffu, 0.0f);
        }
    }
}

static void draw_item_text(const MenuItem *item, const ItemState *state)
{
    char text[128];
    int i;

    switch (item->kind) {
    case MENU_LEVEL_SELECT:
        draw_level_marks(item, state);
        break;
    case MENU_BUTTON:
        if (item->text_index <= 0)
            break;
        batch_text(item->x, item->y - 1.0f,
                   item->z + (strcmp(item->id, "startgame") == 0 ? 1.0f : 0.75f), FONT_CENTER,
                   lang_text(item->text_index, ""), state->hover ? 0xffff0000u : item->text_color);
        break;
    case MENU_LABEL:
        batch_text(item->x, item->y, item->z, FONT_CENTER, lang_text(item->text_index, ""), item->color);
        break;
    case MENU_LABEL_BLOCK:
        for (i = 0; i < item->text_count; i++)
            batch_text(item->x, item->y - (float)i * 32.0f * item->z, item->z, item->align,
                       lang_text(item->text_index + i, ""), item->color);
        break;
    case MENU_CHECKBOX: {
        const bool *value = checkbox_value(item);
        float scale = state->hover ? (m.button ? 0.95f : 0.75f) : 0.65f;
        batch_text(item->x - item->sens_width - 14.0f, item->y + 1.0f, scale, FONT_CENTER,
                   value != NULL && *value ? "[" : "]", 0xffffffffu);
        batch_text(item->x, item->y + 2.0f, item->z + 0.6f, FONT_CENTER,
                   lang_text(item->text_index, ""), 0xff000000u);
        break;
    }
    case MENU_SELECT: {
        int side = select_side(item), value = *select_value(item);
        batch_text(item->x - 4.0f, item->y - 2.0f, 0.6f, FONT_RIGHT, lang_text(item->name_text, ""),
                   0xff000000u);
        if (item->video_mode)
            snprintf(text, sizeof(text), "%s", k_video_modes[value >= 0 && value < VIDEO_MODES ? value : 0]);
        else if (!lang_name(value, text, sizeof(text)))
            text[0] = '\0';
        batch_text(item->x + 2.0f, item->y - 2.0f, 0.7f, FONT_LEFT, text, 0xff0000ffu);
        batch_text(item->x - item->sens_width - 24.0f, item->y - 1.0f, side < 0 ? 0.85f : 0.75f,
                   FONT_CENTER, "<", 0xffffffffu);
        batch_text(item->x + item->sens_width + 26.0f, item->y - 1.0f, side > 0 ? 0.85f : 0.75f,
                   FONT_CENTER, ">", 0xffffffffu);
        break;
    }
    case MENU_SLIDER:
        batch_text(item->x - 8.0f, item->y - 2.0f, 0.6f, FONT_RIGHT, lang_text(item->name_text, ""),
                   0xff000000u);
        batch_text(item->x + state->handle, item->y - 1.0f, item->z + 1.0f, FONT_CENTER, "\\",
                   0xffffffffu);
        break;
    case MENU_HELP_VIEW: {
        PrizeKind kind = (PrizeKind)item->color;
        const PrizeInfo *info;
        float x = item->offset[0] + item->width + item->x - 400.0f;
        float y = 300.0f - item->y + item->offset[1];

        if ((int)kind <= PRIZE_NONE || (int)kind >= PRIZE_KINDS)
            break;
        info = &m.prize_info[kind];
        batch_text(x, y, 0.5f, FONT_LEFT, lang_text(LANG_PRIZES + (int)kind, ""), info->color);
        snprintf(text, sizeof(text), lang_text(23, "Bonus: %d"), info->score);
        batch_text(x, y - 16.0f, 0.5f, FONT_LEFT, text, info->color);
        break;
    }
    default:
        break;
    }
}

static void draw_slider_bar(const MenuItem *item, const ItemState *state)
{
    batch_quad(state->order, state->texture, item->x, item->y + 16.0f, item->x + item->sens_width,
               item->y - 16.0f, 0.0f, 0.934f, 0.25f, 0.995f, 0xffffffffu);
}

static void draw_pictures(void)
{
    int i;

    for (i = 0; i < m.list.count; i++) {
        const MenuItem *item = &m.list.items[i];
        const ItemState *state = &m.states[i];

        if (item->kind == MENU_LEVEL_SELECT) {
            draw_level_select(item, state);
            continue;
        }
        if (item->kind == MENU_HELP_VIEW || item->kind == MENU_LABEL || item->kind == MENU_LABEL_BLOCK)
            continue;
        /* The slider's bar goes on top of its picture. */
        if (item->kind == MENU_SLIDER)
            draw_slider_bar(item, state);
        draw_picture(item, state);
    }
}

/* ------------------------------------------------------------------------ */
/* The help pages' prizes                                                   */
/* ------------------------------------------------------------------------ */

static void load_prize(PrizeKind kind)
{
    const PrizeInfo *info = &m.prize_info[kind];
    char path[96];

    if (m.prize_tried[kind])
        return;
    m.prize_tried[kind] = true;
    if (!info->loaded || info->model[0] == '\0')
        return;
    snprintf(path, sizeof(path), "SkinMeshes/%s", info->model);
    if (xfile_load(&m.prize_models[kind], path))
        asset_model_mesh(&m.prize_meshes[kind], &m.prize_models[kind], RENDER_NOT_LIGHTED, "", "", NULL);
}

/*
 * CMenuHelpView: its prize, turning as it does in the game, in its own
 * corner of the screen (x, y, width, height in pixels of the 800x600
 * screen), seen from (s/2, s, -s) for senswdth s.
 */
static void draw_help_views(void)
{
    int i, pass;
    bool any = false;

    for (i = 0; i < m.list.count; i++) {
        const MenuItem *item = &m.list.items[i];
        PrizeKind kind = (PrizeKind)item->color;
        float s = item->sens_width, left, bottom;
        Mat4 view, projection, *pose;
        const RenderMesh *mesh;

        if (item->kind != MENU_HELP_VIEW || (int)kind <= PRIZE_NONE || (int)kind >= PRIZE_KINDS)
            continue;
        load_prize(kind);
        mesh = &m.prize_meshes[kind];
        if (mesh->buffer == 0)
            continue;
        if (!any) {
            render_clear_depth();
            any = true;
        }
        left = item->x - 400.0f + m.view_width * 0.5f;
        bottom = 300.0f - item->y - item->height + m.view_height * 0.5f;
        render_viewport(m.viewport[0] + (int)(left / m.view_width * (float)m.viewport[2] + 0.5f),
                        m.viewport[1] + (int)(bottom / m.view_height * (float)m.viewport[3] + 0.5f),
                        (int)(item->width / m.view_width * (float)m.viewport[2] + 0.5f),
                        (int)(item->height / m.view_height * (float)m.viewport[3] + 0.5f));
        view = mat4_look_at_lh(vec3(0.5f * s, s, -s), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
        projection = mat4_perspective_fov_lh(3.14159265f / 4.0f, 1.0f, 1.0f, 1000.0f);
        render_set_camera(view, projection, &m.light);
        pose = asset_pose(&m.prize_models[kind], m.states[i].age);
        for (pass = 0; pass < 2; pass++)
            render_draw_posed(mesh, mat4_identity(), pose, pass == 0 ? RENDER_PASS_SOLID : RENDER_PASS_BLENDED,
                              (float)m.time);
        free(pose);
    }
    if (any) {
        render_viewport(m.viewport[0], m.viewport[1], m.viewport[2], m.viewport[3]);
        render_set_camera(mat4_identity(), mat4_identity(), &m.light);
        render_begin_2d(m.view_width, m.view_height);
    }
}

/* ------------------------------------------------------------------------ */
/* The rest of a page                                                       */
/* ------------------------------------------------------------------------ */

/* 0x4163c0 */
static void draw_ticker(uint32_t ms)
{
    int line = m.ticker_line;
    uint32_t t = m.ticker_time += ms, start = k_ticker[line].start, end = k_ticker[line].end;
    const char *text = lang_text(0xd0 + line, "");
    uint32_t color = k_ticker[line].color;

    if (t > start && t < end) {
        if (!k_ticker[line].runs) {
            uint32_t alpha = color >> 24;
            if (t - start < 1000)
                alpha = (t - start) >> 2;
            else if (end - t < 1000)
                alpha = ((end - t) >> 2) + 1;
            batch_text(0.0f, -241.0f, 0.75f, FONT_CENTER, text, (color & 0xffffffu) | (alpha & 0xffu) << 24);
        } else {
            /* It runs left at 0.03 a millisecond, from x 200 until its end
               is past -200, then the next line starts. */
            m.ticker_x -= (float)ms * 0.03f;
            if (m.ticker_x + font_text_width(&m.text_font, text) * 0.75f < -200.0f) {
                m.ticker_time = end + 1;
                m.ticker_x = 200.0f;
            } else {
                batch_text_clipped(false, m.ticker_x, -241.0f, 0.75f, FONT_LEFT, text, color, 200.0f);
            }
        }
    }
    if (m.ticker_time > end) {
        m.ticker_line++;
        if (m.ticker_line > 6) {
            m.ticker_line = 0;
            m.ticker_time = 0;
        }
    }
}

/* 0x426b10: one place in the table. Every other line is a little darker,
   the place just added flashes red and white, and lines fade out at the
   top and bottom of the panel. */
static void draw_score(int place, float y, uint32_t color)
{
    char text[32];
    float alpha = (float)(color >> 24);

    if (y > 91.0f || y < -159.0f)
        return;
    if (place == m.highlight)
        color = (color & 0xff000000u) | ((m.time / 500) & 1 ? 0xffffffu : 0xff0000u);
    if (place & 1)
        color &= 0xffccccccu;
    if (y > 66.0f)
        alpha *= (91.0f - y) / 25.0f;
    else if (y < -134.0f)
        alpha *= (y + 159.0f) / 25.0f;
    color = (color & 0xffffffu) | (uint32_t)alpha << 24;
    snprintf(text, sizeof(text), "%d.", place + 1);
    batch_text(-180.0f, y, 0.75f, FONT_LEFT, text, color);
    batch_text(-130.0f, y, 0.75f, FONT_LEFT, m.scores.names[place], color);
    snprintf(text, sizeof(text), "%d", m.scores.scores[place]);
    batch_text(180.0f, y, 0.75f, FONT_RIGHT, text, color);
}

/*
 * 0x416970: the first eight places fade in over a second; after five
 * seconds the table runs up until its end is in view, waits, fades out
 * and starts again.
 */
static void draw_score_table(uint32_t ms)
{
    uint32_t t = m.page_time;
    int place;

    if (m.page == PAGE_SCORES) {
        if (t < 5000) {
            uint32_t color = t < 1000 ? (t >> 2) << 24 | 0xffffffu : 0xffffffffu;
            for (place = 0; place < 8; place++)
                if (m.scores.scores[place] > 0)
                    draw_score(place, m.scroll - (float)place * 28.0f, color);
        } else if (t > 508000) {
            m.page_time = 0;
            m.scroll = 66.0f;
        } else if (t <= 505000 && t > 504000) {
            uint32_t color = (0xffu - ((t - 504000) >> 2)) << 24 | 0xffffffu;
            for (place = 0; place < SCORES_PLACES; place++)
                if (m.scores.scores[place] > 0)
                    draw_score(place, m.scroll - (float)place * 28.0f, color);
        } else if (t <= 504000) {
            bool done = t > 500000;
            for (place = 0; place < SCORES_PLACES; place++) {
                float y = m.scroll - (float)place * 28.0f;
                if (m.scores.scores[place] > 0)
                    draw_score(place, y, 0xffffffffu);
                else if (!done && y > -159.0f)
                    done = true; /* the end of the table is in view */
            }
            if (t <= 500000) {
                if (done)
                    m.page_time = 500000;
                else
                    m.scroll += (float)ms * 0.075f;
            }
        }
    }
    batch_text(0.0f, 123.0f, 0.9f, FONT_CENTER, lang_text(22, "High Scores"), 0xff000000u);
}

/* 0x417c60 */
static void draw_enter_name(uint32_t ms)
{
    char text[32];

    m.pulse += (float)ms * 0.005f;
    batch_text(0.0f, 109.0f, 0.95f, FONT_CENTER, lang_text(8, "Game Over"), 0xff000000u);
    batch_text(0.0f, 50.0f, 0.85f, FONT_CENTER, lang_text(9, "New High Score:"), 0xff00ffffu);
    snprintf(text, sizeof(text), "%d", m.score);
    batch_text(0.0f, 0.0f, 1.0f + 0.25f * sinf(m.pulse), FONT_CENTER, text, 0xffffff00u);
    batch_text(0.0f, -50.0f, 0.85f, FONT_CENTER, lang_text(10, "Enter Your Name:"), 0xff00ffffu);
    snprintf(text, sizeof(text), "%s%s", m.name, (m.time / 500) & 1 ? " " : "_");
    batch_text(10.0f, -108.0f, 1.0f, FONT_CENTER, text, 0xffffffffu);
}

/* Cursor.dds: 16 frames of a turning pointer, 50 ms each, its point at
   the top left. It goes over the black bars too, out to the window's
   edges, so they can be reached; this draws what is queued first. */
static void draw_cursor(void)
{
    int frame = (int)(m.time / 50) & 15;
    float u = (float)(frame / 4) * 0.25f + 1.0f / 256.0f, v = (float)(frame % 4) * 0.25f + 1.0f / 256.0f;
    float x = m.mouse_x, y = m.mouse_y;
    float width = m.view_width * (float)m.screen_width / (float)(m.viewport[2] > 0 ? m.viewport[2] : 1);
    float height = m.view_height * (float)m.screen_height / (float)(m.viewport[3] > 0 ? m.viewport[3] : 1);
    float right = width * 0.5f - 32.0f, top = height * 0.5f;

    batch_draw();
    render_viewport(0, 0, m.screen_width, m.screen_height);
    render_begin_2d(width, height);
    if (x > right) x = right;
    if (x < -width * 0.5f) x = -width * 0.5f;
    if (y > top) y = top;
    if (y < 32.0f - top) y = 32.0f - top;
    batch_quad(ORDER_CURSOR, m.cursor_texture, x, y, x + 32.0f, y - 32.0f, u, v, u + 0.25f - 2.0f / 256.0f,
               v + 0.25f - 2.0f / 256.0f, 0xffffffffu);
}

/* Menu.x: the frame ("Interface"), over a sheet of tiles ("Fon") that
   drifts round in a circle. */
static void draw_backdrop(uint32_t ms)
{
    int i;

    Mat4 pose[64], *rest;
    int pass;

    if (m.menu_mesh.buffer == 0 || m.menu_model.part_count > 64)
        return;
    rest = asset_pose(&m.menu_model, 0);
    if (rest == NULL)
        return;
    m.fon_time += (float)ms * 0.0005f;
    /* Each frame is drawn on its own (0x421b20): one at a time, the
       other's parts moved far out of sight. */
    for (pass = 0; pass < 2; pass++) {
        const char *frame = pass == 0 ? "Fon" : "Interface";
        Mat4 place = pass == 0 ? mat4_translation(sinf(m.fon_time) * 70.0f, cosf(m.fon_time) * 90.0f, 0.0f)
                               : mat4_identity();
        for (i = 0; i < m.menu_model.part_count; i++)
            pose[i] = mat4_multiply(rest[i], strcmp(m.menu_model.parts[i].frame, frame) == 0
                                                 ? place
                                                 : mat4_translation(1e6f, 1e6f, 0.0f));
        render_draw_posed(&m.menu_mesh, mat4_identity(), pose, RENDER_PASS_ALL, 0.0f);
    }
    free(rest);
}

/* ------------------------------------------------------------------------ */
/* The screen                                                               */
/* ------------------------------------------------------------------------ */

bool menu_screen_init(SDL_Window *window, Params *params, bool intro)
{
    unsigned char *data;
    size_t size;
    const char *user;

    memset(&m, 0, sizeof(m));
    m.window = window;
    m.params = params;
    m.intro_enabled = intro;
    m.highlight = -1;
    menu_list_clear(&m.list);
    snprintf(m.pad, sizeof(m.pad), "Pads/common.txt");
    if (!textureset_load(&m.textures, "textureset.txt"))
        log_warn("menu: no textureset.txt");
    memcpy(m.light.direction, m.textures.light_dir, sizeof(m.light.direction));
    memcpy(m.light.ambient, m.textures.ambient, sizeof(m.light.ambient));
    memcpy(m.light.diffuse, m.textures.diffuse, sizeof(m.light.diffuse));
    font_load(&m.text_font, "Text/text.txt");
    font_load(&m.service_font, "Text/service.txt");
    m.font_texture = asset_texture("font.png");
    m.service_texture = asset_texture("service.png");
    m.cursor_texture = asset_texture("Cursor.dds");
    m.splash_texture = render_texture("splash.png");
    if (xfile_load(&m.menu_model, "SkinMeshes/Menu.x"))
        asset_model_mesh(&m.menu_mesh, &m.menu_model,
                         RENDER_ALPHA | RENDER_Z_DISABLE | RENDER_NO_DEPTH_TEST | RENDER_NOT_LIGHTED |
                             RENDER_TWO_SIDED,
                         "", "", NULL);
    else
        log_warn("menu: can't load SkinMeshes/Menu.x");
    play_load_prize_info(m.prize_info);
    data = userfile_read("score.dat", &size);
    if (data != NULL && !scores_decode(&m.scores, data, size))
        log_warn("menu: score.dat is not a score table");
    free(data);
    /* The name starts as the Windows user's (0x419228). */
    user = getenv("USERNAME");
    if (user == NULL)
        user = getenv("USER");
    snprintf(m.name, sizeof(m.name), "%s", user != NULL && user[0] != '\0' ? user : "Unnamed");
    m.ticker_x = 200.0f;
    return true;
}

void menu_screen_shutdown(void)
{
    int i;

    stop_screw();
    intro_stop();
    render_mesh_destroy(&m.mesh);
    render_mesh_destroy(&m.menu_mesh);
    xfile_free(&m.menu_model);
    for (i = 0; i < PRIZE_KINDS; i++) {
        render_mesh_destroy(&m.prize_meshes[i]);
        xfile_free(&m.prize_models[i]);
    }
    menu_list_free(&m.list);
    free(m.states);
    textureset_free(&m.textures);
    memset(&m, 0, sizeof(m));
}

void menu_screen_show_splash(void)
{
    m.page = PAGE_SPLASH;
    m.page_time = 0;
}

void menu_screen_show_page(const char *list)
{
    if (strstr(list, "highscores") != NULL)
        show_scores(-1);
    else if (strcmp(list, "packs") == 0)
        show_packs();
    else if (strcmp(list, "entername") == 0)
        show_enter_name();
    else
        navigate(list);
    audio_play_music("Music/menu.txt");
}

void menu_screen_show_main(void)
{
    show_main();
}

void menu_screen_show_game_over(int score)
{
    m.score = score;
    if (scores_qualifies(&m.scores, score))
        show_enter_name();
    else
        show_main();
}

void menu_screen_show_end_of_pack(int score)
{
    m.score = score;
    navigate("Menu/lendgame.txt");
    audio_play_music("Music/enter.txt");
}

void menu_screen_set_wide(bool wide)
{
    m.wide = wide;
}

void menu_screen_game(const char **folder, int *level, const char **pad)
{
    *folder = m.pack;
    *level = m.level;
    *pad = m.pad;
}

void menu_screen_handle_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_KEYDOWN:
        if (m.page == PAGE_INTRO || m.page == PAGE_SPLASH)
            break;
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            show_main();
        } else if (m.page == PAGE_ENTER_NAME) {
            size_t length = strlen(m.name);
            if (event->key.keysym.sym == SDLK_BACKSPACE && length > 0)
                m.name[length - 1] = '\0';
            else if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER)
                enter_name();
        }
        break;
    case SDL_TEXTINPUT:
        if (m.page == PAGE_ENTER_NAME) {
            const unsigned char *c;
            for (c = (const unsigned char *)event->text.text; *c != '\0'; c++) {
                size_t length = strlen(m.name);
                if (*c >= 32 && *c < 127 && length < NAME_LENGTH) {
                    m.name[length] = (char)*c;
                    m.name[length + 1] = '\0';
                }
            }
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT)
            m.button_down = true;
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT)
            m.button_down = false;
        break;
    default:
        break;
    }
    /* Any key or button skips the intro (0x41bed3). */
    if (m.page == PAGE_INTRO && (event->type == SDL_KEYDOWN || event->type == SDL_MOUSEBUTTONDOWN))
        show_main();
}

static void update_mouse(void)
{
    int x, y, w, h;

    /* A release while a level had the mouse never reached us. */
    if (!(SDL_GetMouseState(&x, &y) & SDL_BUTTON_LMASK))
        m.button_down = false;
    if (!m.button_down)
        m.wait_release = false;
    m.button = m.button_down && !m.wait_release;
    SDL_GetWindowSize(m.window, &w, &h);
    if (w <= 0 || h <= 0 || m.viewport[2] <= 0 || m.viewport[3] <= 0)
        return;
    /* From window points to pixels from the bottom left, then into the menus. */
    {
        float px = (float)x * (float)m.screen_width / (float)w;
        float py = (float)m.screen_height - (float)y * (float)m.screen_height / (float)h;
        m.mouse_x = ((px - (float)m.viewport[0]) / (float)m.viewport[2] - 0.5f) * m.view_width;
        m.mouse_y = ((py - (float)m.viewport[1]) / (float)m.viewport[3] - 0.5f) * m.view_height;
    }
}

/* splash.png at its own size, fading out over about a second (0x41ab9a). */
static void draw_splash(uint32_t ms)
{
    uint32_t alpha;

    m.page_time += ms;
    alpha = m.page_time / 4 < 255 ? 255 - m.page_time / 4 : 0;
    batch_quad(0, m.splash_texture, -256.0f, 256.0f, 256.0f, -256.0f, 0.0f, 0.0f, 1.0f, 1.0f,
               alpha << 24 | 0xffffffu);
    batch_draw();
    if (alpha == 0) {
        if (m.intro_enabled && intro_start()) {
            m.page = PAGE_INTRO;
            m.page_time = 0;
            audio_play_music("Music/intro.txt");
        } else {
            show_main();
        }
    }
}

MenuAction menu_screen_frame(int width, int height, uint32_t ms)
{
    Mat4 identity = mat4_identity();

    m.action = MENU_ACTION_NONE;
    m.time += ms;
    /* Sized like the HUD: 600 high, wider on a wider screen. */
    m.view_width = 800.0f;
    m.view_height = 600.0f;
    m.screen_width = width;
    m.screen_height = height;
    m.viewport[0] = m.viewport[1] = 0;
    m.viewport[2] = width;
    m.viewport[3] = height;
    if (m.wide) {
        /* 600 high, wider on a wider screen, like the HUD. */
        if (width * 3 >= height * 4)
            m.view_width = 600.0f * (float)width / (float)(height > 0 ? height : 1);
        else
            m.view_height = 800.0f * (float)height / (float)(width > 0 ? width : 1);
    } else if (width * 3 > height * 4) {
        m.viewport[2] = height * 4 / 3;
        m.viewport[0] = (width - m.viewport[2]) / 2;
    } else if (width * 3 < height * 4) {
        m.viewport[3] = width * 3 / 4;
        m.viewport[1] = (height - m.viewport[3]) / 2;
    }
    update_mouse();

    render_begin(width, height, identity, identity, &m.light);
    render_viewport(m.viewport[0], m.viewport[1], m.viewport[2], m.viewport[3]);
    render_begin_2d(m.view_width, m.view_height);
    batch_begin();

    if (m.page == PAGE_SPLASH) {
        draw_splash(ms);
        return m.action;
    }
    if (m.page == PAGE_INTRO) {
        if (!intro_frame(width, height, m.viewport, ms, &m.light))
            show_main();
        else
            return m.action;
        render_begin(width, height, identity, identity, &m.light);
        render_viewport(m.viewport[0], m.viewport[1], m.viewport[2], m.viewport[3]);
        render_begin_2d(m.view_width, m.view_height);
    }

    m.page_time += ms;
    update_items(ms);
    if (m.action != MENU_ACTION_NONE)
        return m.action;

    draw_backdrop(ms);
    draw_pictures();
    batch_draw();
    draw_help_views();
    {
        int i;
        for (i = 0; i < m.list.count; i++)
            draw_item_text(&m.list.items[i], &m.states[i]);
    }
    if (m.page == PAGE_SCORES || m.page == PAGE_GLOBAL_SCORES)
        draw_score_table(ms);
    if (m.page == PAGE_ENTER_NAME)
        draw_enter_name(ms);
    draw_ticker(ms);
    draw_cursor();
    batch_draw();
    return m.action;
}
