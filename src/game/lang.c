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

static struct {
    char *data;
    char **lines;
    int count;
} g_lang;

typedef struct Names {
    char names[MAX_LANGUAGES][128];
    int count;
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

static int compare_names(const void *a, const void *b)
{
    return compare_nocase((const char *)a, (const char *)b);
}

bool lang_load(int index)
{
    Names list;
    char path[160];
    char *p;
    int capacity = 0;

    lang_free();
    list.count = 0;
    vfs_list("Languages", add_name, &list);
    if (list.count == 0) {
        log_warn("lang: no languages in the Languages folder");
        return false;
    }
    /* The original lists them with FindFirstFile, which goes by name on NTFS. */
    qsort(list.names, (size_t)list.count, sizeof(list.names[0]), compare_names);
    if (index < 0 || index >= list.count)
        index = 0;
    snprintf(path, sizeof(path), "Languages/%s", list.names[index]);
    g_lang.data = vfs_read(path, NULL);
    if (g_lang.data == NULL) {
        log_warn("lang: can't read %s", path);
        return false;
    }
    /* Split into lines, dropping the first (the "/// SYSTEM ///" header). */
    p = strchr(g_lang.data, '\n');
    while (p != NULL) {
        char *line = p + 1, *end;

        p = strchr(line, '\n');
        end = p != NULL ? p : line + strlen(line);
        if (end > line && end[-1] == '\r')
            end--;
        *end = '\0';
        if (p == NULL && line[0] == '\0')
            break;
        if (g_lang.count == capacity) {
            char **grown;
            capacity = capacity ? capacity * 2 : 64;
            grown = realloc(g_lang.lines, (size_t)capacity * sizeof(*grown));
            if (grown == NULL)
                break;
            g_lang.lines = grown;
        }
        g_lang.lines[g_lang.count++] = line;
    }
    log_info("lang: %s, %d strings", path, g_lang.count);
    return true;
}

void lang_free(void)
{
    free(g_lang.lines);
    free(g_lang.data);
    memset(&g_lang, 0, sizeof(g_lang));
}

const char *lang_text(int index, const char *fallback)
{
    if (index < 0 || index >= g_lang.count)
        return fallback;
    return g_lang.lines[index];
}
