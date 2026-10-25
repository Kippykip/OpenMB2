/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "lang.h"
#include "../core/log.h"
#include "../core/vfs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LANGUAGES 32
#define LANG_SLOTS 512

static struct {
    char *data;
    const char *slots[LANG_SLOTS];
    char name[128]; /* "English" */
} g_lang;

/* Where each section's lines start (0x418950). */
static const struct {
    const char *header;
    int slot;
} k_sections[] = {
    { "/// OPTIONS ///", 0x30 },  { "/// NAG-SCREEN ///", 0xc0 }, { "/// PACKS ///", 0xa0 },
    { "/// INFO ///", 0x60 },     { "/// TRIAL ///", 0x70 },      { "/// CREDITS ///", 0xe0 },
    { "/// RUNNINGSTRING ///", 0xd0 }, { "/// ENDGAME ///", 0x20 }, { "/// PRIZES ///", 0x80 },
};

typedef struct Names {
    char names[MAX_LANGUAGES][128];
    int count;
    int game_count; /* the game's own, before those only the OpenMB2 folder has */
} Names;

static int compare_nocase(const char *a, const char *b)
{
    while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void add_name(const char *name, bool is_dir, void *user)
{
    Names *list = user;
    size_t length = strlen(name);

    if (is_dir || length < 4 || compare_nocase(name + length - 4, ".txt") != 0 ||
        list->count >= MAX_LANGUAGES)
        return;
    snprintf(list->names[list->count++], sizeof(list->names[0]), "%s", name);
}

static bool is_added(const char *name)
{
    char path[160];

    snprintf(path, sizeof(path), "Languages/%s", name);
    return vfs_is_added(path);
}

/* By name, with the languages only the OpenMB2 folder has after the
   game's, so params.txt's numbers mean the same to both games. */
static int compare_names(const void *a, const void *b)
{
    bool added_a = is_added(a), added_b = is_added(b);

    if (added_a != added_b)
        return added_a ? 1 : -1;
    return compare_nocase((const char *)a, (const char *)b);
}

/* The original lists them with FindFirstFile, which goes by name on NTFS. */
static void list_languages(Names *list)
{
    int i;

    list->count = 0;
    vfs_list("Languages", add_name, list);
    qsort(list->names, (size_t)list->count, sizeof(list->names[0]), compare_names);
    for (i = 0; i < list->count && !is_added(list->names[i]); i++)
        ;
    list->game_count = i;
}

int lang_game_count(void)
{
    Names list;
    list_languages(&list);
    return list.game_count;
}

int lang_find(const char *name)
{
    char found[128];
    int i;

    for (i = 0; name != NULL && name[0] != '\0' && lang_name(i, found, sizeof(found)); i++)
        if (compare_nocase(found, name) == 0)
            return i;
    return -1;
}

int lang_count(void)
{
    Names list;
    list_languages(&list);
    return list.count;
}

bool lang_name(int index, char *out, size_t size)
{
    Names list;
    size_t length;

    list_languages(&list);
    if (index < 0 || index >= list.count || size == 0)
        return false;
    snprintf(out, size, "%s", list.names[index]);
    length = strlen(out);
    if (length >= 4)
        out[length - 4] = '\0';
    return true;
}

bool lang_load(int index)
{
    Names list;
    char path[160];
    char *p;
    int slot = 0;

    lang_free();
    list_languages(&list);
    if (list.count == 0) {
        log_warn("lang: no languages in the Languages folder");
        return false;
    }
    if (index < 0 || index >= list.count)
        index = 0;
    snprintf(path, sizeof(path), "Languages/%s", list.names[index]);
    g_lang.data = vfs_read(path, NULL);
    lang_name(index, g_lang.name, sizeof(g_lang.name));
    if (g_lang.data == NULL) {
        log_warn("lang: can't read %s", path);
        return false;
    }
    /* Each line goes in the next slot; a section line, stored like any
       other, sends the next line to its section's first slot. */
    for (p = g_lang.data; p != NULL && *p != '\0' && slot < LANG_SLOTS;) {
        char *line = p, *end = strchr(p, '\n');
        size_t i;
        bool section = false;

        p = end != NULL ? end + 1 : NULL;
        if (end == NULL)
            end = line + strlen(line);
        if (end > line && end[-1] == '\r')
            end--;
        *end = '\0';
        g_lang.slots[slot] = line;
        for (i = 0; i < sizeof(k_sections) / sizeof(k_sections[0]); i++) {
            if (strcmp(line, k_sections[i].header) == 0) {
                slot = k_sections[i].slot;
                section = true;
            }
        }
        if (!section)
            slot++;
    }
    log_info("lang: %s", path);
    return true;
}

void lang_font(char *text, size_t text_size, char *image, size_t image_size)
{
    char path[200];

    snprintf(text, text_size, "Text/Text_%s.txt", g_lang.name);
    if (g_lang.name[0] == '\0' || !vfs_exists(text))
        snprintf(text, text_size, "Text/text.txt");
    snprintf(image, image_size, "Font_%s.png", g_lang.name);
    snprintf(path, sizeof(path), "Textures/%s", image);
    if (g_lang.name[0] == '\0' || !vfs_exists(path))
        snprintf(image, image_size, "font.png");
}

void lang_free(void)
{
    free(g_lang.data);
    memset(&g_lang, 0, sizeof(g_lang));
}

const char *lang_text(int index, const char *fallback)
{
    if (index < 0 || index >= LANG_SLOTS || g_lang.slots[index] == NULL)
        return fallback;
    return g_lang.slots[index];
}
