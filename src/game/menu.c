/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "menu.h"
#include "font.h"
#include "../core/config.h"
#include "../core/log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    MenuKind kind;
} k_classes[] = {
    { "CMenuItemButton", MENU_BUTTON },        { "CMenuItemCheckbox", MENU_CHECKBOX },
    { "CMenuItemSelect", MENU_SELECT },        { "CMenuItemSlider", MENU_SLIDER },
    { "CMenuItemPanel", MENU_PANEL },          { "CMenuItemLabelBlock", MENU_LABEL_BLOCK },
    { "CMenuItemLabel", MENU_LABEL },          { "CMenuItemLevelSelect", MENU_LEVEL_SELECT },
    { "CMenuHelpView", MENU_HELP_VIEW },
};

/* The settings whose id names them, and the language line with the name
   (0x41d8d7: the original copies the line in as it reads the file). */
static const struct {
    const char *id;
    int text;
} k_setting_names[] = {
    { "sound", 50 }, { "music", 51 }, { "joystick", 52 }, { "hallfame", 53 },
    { "infravision", 54 }, { "msensivity", 56 }, { "language", 49 },
};

static bool same(const char *a, const char *b)
{
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* The base item (0x41cee0), then what each class changes. */
static void set_defaults(MenuItem *item, MenuKind kind)
{
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->sens_width = 128.0f;
    item->size[0] = item->size[1] = 1.0f;
    item->color = 0xffffffffu;
    item->text_color = 0xffffffffu;
    item->align = FONT_CENTER;
    item->name_text = -1;
    snprintf(item->texture, sizeof(item->texture), "none");
    if (kind == MENU_BUTTON)
        item->text_color = 0xff000000u; /* 0x41ddd0 */
    else if (kind == MENU_SELECT)
        item->name_text = 49;           /* 0x41e9d0: "Languages" */
}

static float number(const char *value)
{
    return value != NULL ? strtof(value, NULL) : 0.0f;
}

bool menu_item_load(MenuItem *item, const char *game_path)
{
    Config cfg;
    MenuKind kind = MENU_ITEM;
    size_t i;
    int e;

    if (!config_load(&cfg, game_path)) {
        log_warn("menu: can't read %s", game_path);
        return false;
    }
    for (i = 0; i < sizeof(k_classes) / sizeof(k_classes[0]); i++) {
        if (same(cfg.class_name, k_classes[i].name))
            kind = k_classes[i].kind;
    }
    set_defaults(item, kind);

    /* In file order, as 0x41d5f0 reads it. */
    for (e = 0; e < cfg.count; e++) {
        const char *key = cfg.entries[e].key, *value = cfg.entries[e].value;
        float f[3];

        if (same(key, "width"))
            item->width = number(value);
        else if (same(key, "height"))
            item->height = number(value);
        else if (same(key, "senswdth"))
            item->sens_width = number(value);
        else if (same(key, "videomode")) {
            item->video_mode = true;
            item->name_text = 48; /* "Video Mode" */
        } else if (same(key, "texture") && value != NULL)
            snprintf(item->texture, sizeof(item->texture), "%s", value);
        else if (same(key, "offset") && config_parse_floats(value, f, 2) == 2) {
            item->offset[0] = f[0];
            item->offset[1] = f[1];
        } else if (same(key, "flipped"))
            item->flipped = true;
        else if (same(key, "size") && config_parse_floats(value, f, 2) == 2) {
            item->size[0] = f[0];
            item->size[1] = f[1];
            item->has_size = true;
        } else if (same(key, "color") && value != NULL)
            item->color = (uint32_t)strtoul(value, NULL, 16);
        else if (same(key, "levelindex") && value != NULL)
            item->level_index = atoi(value);
        else if (same(key, "id") && value != NULL) {
            snprintf(item->id, sizeof(item->id), "%s", value);
            for (i = 0; i < sizeof(k_setting_names) / sizeof(k_setting_names[0]); i++) {
                if (strcmp(item->id, k_setting_names[i].id) == 0)
                    item->name_text = k_setting_names[i].text;
            }
        } else if (same(key, "script") && value != NULL)
            snprintf(item->script, sizeof(item->script), "%s", value);
        else if (same(key, "texturex"))
            item->texture_x = number(value);
        else if (same(key, "texturey"))
            item->texture_y = number(value);
        else if (same(key, "pos") && config_parse_floats(value, f, 2) == 2) {
            item->x = f[0];
            item->y = f[1];
        } else if (same(key, "po3") && config_parse_floats(value, f, 3) == 3) {
            item->x = f[0];
            item->y = f[1];
            item->z = f[2];
        } else if (same(key, "leftalign"))
            item->align = FONT_LEFT;
        else if (same(key, "rightalign"))
            item->align = FONT_RIGHT;
        else if (same(key, "textindex") && value != NULL)
            item->text_index = atoi(value);
        else if (same(key, "textcount") && value != NULL)
            item->text_count = atoi(value);
    }
    config_free(&cfg);
    return true;
}

MenuItem *menu_list_add(MenuList *list, const char *game_path)
{
    MenuItem item;

    if (!menu_item_load(&item, game_path))
        return NULL;
    if (list->count == list->capacity) {
        int capacity = list->capacity > 0 ? list->capacity * 2 : 16;
        MenuItem *grown = realloc(list->items, (size_t)capacity * sizeof(*grown));
        if (grown == NULL)
            return NULL;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count] = item;
    return &list->items[list->count++];
}

bool menu_list_load(MenuList *list, const char *game_path)
{
    Config cfg;
    int e;

    if (!config_load(&cfg, game_path)) {
        log_warn("menu: can't read %s", game_path);
        return false;
    }
    for (e = 0; e < cfg.count; e++) {
        const char *key = cfg.entries[e].key, *value = cfg.entries[e].value;

        if (value == NULL)
            continue;
        if (same(key, "menuitem"))
            menu_list_add(list, value);
        else if (same(key, "updatetexture") && list->update_texture_count < 4)
            snprintf(list->update_textures[list->update_texture_count++], MENU_PATH_SIZE, "%s",
                     value);
        else if (same(key, "intro"))
            list->intro = atoi(value);
        else if (same(key, "build"))
            snprintf(list->build, sizeof(list->build), "%s", value);
        /* cleartexture: frees textures the original won't need for a
           while; there's nothing to gain from that here. */
    }
    config_free(&cfg);
    return true;
}

void menu_list_clear(MenuList *list)
{
    list->count = 0;
    list->update_texture_count = 0;
    list->intro = -1;
    list->build[0] = '\0';
}

void menu_list_free(MenuList *list)
{
    free(list->items);
    memset(list, 0, sizeof(*list));
    list->intro = -1;
}
