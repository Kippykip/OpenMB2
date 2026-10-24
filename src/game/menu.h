/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * menu.h - the menu files in Menu/ and in each pack's Menu/ folder.
 *
 * A list file names the items of one page:
 *
 *     List
 *     {
 *       menuitem: Menu/startgame.txt
 *       updatetexture: Levels/levels1.jpg
 *     }
 *
 * and each item is a file of its own (0x414780, 0x41d5f0):
 *
 *     CMenuItemButton
 *     {
 *       width: 187              half its width, in the 800x600 screen
 *       height: 40              half its height
 *       texture: Options.png
 *       offset: 0.625 0.8125    top left of its picture in the texture...
 *       size: 0.375 0.1875      ...and its size; without them, the
 *                               texture script's xfactor / yfactor cell
 *                               at texturex / texturey
 *       flipped                 mirror the picture
 *       id: navigate            what clicking it does
 *       script: Menu/lcontrols.txt
 *       textindex: 2            its words, from the language file
 *       pos: 0 36               its centre, y up from the middle
 *     }
 *
 * The screen is 800x600 with (0, 0) in the middle. Nothing here draws.
 */
#ifndef OPENMB2_MENU_H
#define OPENMB2_MENU_H

#include <stdbool.h>
#include <stdint.h>

typedef enum MenuKind {
    MENU_ITEM,          /* an unknown class: just its picture */
    MENU_BUTTON,        /* CMenuItemButton */
    MENU_CHECKBOX,      /* CMenuItemCheckbox */
    MENU_SELECT,        /* CMenuItemSelect: < value > */
    MENU_SLIDER,        /* CMenuItemSlider */
    MENU_PANEL,         /* CMenuItemPanel: a picture */
    MENU_LABEL_BLOCK,   /* CMenuItemLabelBlock: several lines of text */
    MENU_LABEL,         /* CMenuItemLabel: one line of text */
    MENU_LEVEL_SELECT,  /* CMenuItemLevelSelect: 20 levels to pick from */
    MENU_HELP_VIEW      /* CMenuHelpView: a prize turning, with its name */
} MenuKind;

#define MENU_PATH_SIZE 260

typedef struct MenuItem {
    MenuKind kind;
    float x, y, z;              /* pos: / po3:; z adds to the text's scale */
    float width, height;        /* half sizes */
    float sens_width;           /* senswdth: (the camera distance for a help view) */
    char texture[128];
    bool has_size;              /* offset: and size: given */
    float offset[2], size[2];
    float texture_x, texture_y; /* which cell of the texture script's grid */
    bool flipped;
    uint32_t color;             /* the picture's colour; a help view's prize kind */
    uint32_t text_color;        /* a button's words: black */
    char id[64];
    char script[MENU_PATH_SIZE];
    int text_index, text_count;
    int align;                  /* FONT_LEFT / FONT_CENTER / FONT_RIGHT */
    int level_index;            /* a level select's first level */
    int name_text;              /* a setting's name, from the language file; -1 none */
    bool video_mode;            /* a select for the video mode, not the language */
} MenuItem;

typedef struct MenuList {
    MenuItem *items;
    int count, capacity;
    char update_textures[4][MENU_PATH_SIZE];
    int update_texture_count;
    int intro;          /* intro: (-1 when not given) */
    char build[64];     /* build: ("" when not given) */
} MenuList;

/* Read one item file; false if it can't be read. */
bool menu_item_load(MenuItem *item, const char *game_path);

/* Read a list file, adding its items to list (which may hold some already). */
bool menu_list_load(MenuList *list, const char *game_path);

/* Add an item from its file, returning it (NULL on failure). */
MenuItem *menu_list_add(MenuList *list, const char *game_path);

void menu_list_clear(MenuList *list);
void menu_list_free(MenuList *list);

#endif
