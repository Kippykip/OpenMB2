/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "vfs.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define VFS_MAX_PATH  1024
#define VFS_MAX_DEPTH 8
/* Files in this folder of the game folder are read in place of the game's
   own, so OpenMB2 can add to the game without changing its files. */
#define VFS_ADDED     "openmb2/"

typedef struct VfsEntry {
    char *key;   /* normalised path, used for lookups */
    char *real;  /* path relative to the root, with the real case */
    bool is_dir;
    bool added;  /* from the OpenMB2 folder, standing in for the game's own */
} VfsEntry;

static struct {
    char *root;
    VfsEntry *entries;
    int count;
    int capacity;
    int file_count;
} g_vfs;

/* ------------------------------------------------------------------------ */
/* Path helpers                                                             */
/* ------------------------------------------------------------------------ */

void vfs_normalise(const char *game_path, char *out, size_t out_size)
{
    size_t n = 0;
    const char *p = game_path;

    if (out_size == 0)
        return;

    while (*p != '\0' && n + 1 < out_size) {
        char c = *p++;

        if (c == '\\')
            c = '/';

        if (c == '/') {
            /* Drop leading and repeated slashes. */
            if (n == 0 || out[n - 1] == '/')
                continue;
        } else if (c == '.' && (n == 0 || out[n - 1] == '/') &&
                   (*p == '/' || *p == '\\')) {
            /* Drop "./" components. */
            p++;
            continue;
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[n++] = c;
    }

    /* Drop a trailing slash so "Menu/" and "Menu" are the same folder. */
    if (n > 0 && out[n - 1] == '/')
        n--;
    out[n] = '\0';
}

static char *copy_string(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL)
        memcpy(copy, s, len);
    return copy;
}

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    if (a[0] == '\0')
        snprintf(out, out_size, "%s", b);
    else if (b[0] == '\0')
        snprintf(out, out_size, "%s", a);
    else
        snprintf(out, out_size, "%s/%s", a, b);
}

/* ------------------------------------------------------------------------ */
/* Building the index                                                       */
/* ------------------------------------------------------------------------ */

static void push_entry(const char *key, const char *relative, bool is_dir, bool added)
{
    VfsEntry *entry;

    if (g_vfs.count == g_vfs.capacity) {
        int new_capacity = g_vfs.capacity ? g_vfs.capacity * 2 : 256;
        VfsEntry *grown = realloc(g_vfs.entries, (size_t)new_capacity * sizeof(*grown));
        if (grown == NULL) {
            log_error("vfs: out of memory");
            return;
        }
        g_vfs.entries = grown;
        g_vfs.capacity = new_capacity;
    }

    entry = &g_vfs.entries[g_vfs.count++];
    entry->key = copy_string(key);
    entry->real = copy_string(relative);
    entry->is_dir = is_dir;
    entry->added = added;
    if (!is_dir && !added)
        g_vfs.file_count++;
}

static void add_entry(const char *relative, bool is_dir)
{
    char key[VFS_MAX_PATH];
    size_t added = strlen(VFS_ADDED);

    vfs_normalise(relative, key, sizeof(key));
    push_entry(key, relative, is_dir, false);
    /* A file under OpenMB2/ is also listed under its path without it. */
    if (strncmp(key, VFS_ADDED, added) == 0 && key[added] != '\0')
        push_entry(key + added, relative, is_dir, true);
}

static void scan_folder(const char *relative, int depth);

/* Called once per directory entry found while scanning. */
static void scan_child(const char *relative, const char *name, bool is_dir, int depth)
{
    char child[VFS_MAX_PATH];

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return;

    join_path(child, sizeof(child), relative, name);
    add_entry(child, is_dir);
    if (is_dir)
        scan_folder(child, depth + 1);
}

#ifdef _WIN32

/*
 * Windows: file names are UTF-16, and the "A" functions would turn any
 * character outside the ANSI code page into '?' (Levels0r has a level
 * named with a Cyrillic letter). So we use the wide functions and keep
 * every path in UTF-8 inside the engine.
 */
static bool utf8_to_wide(const char *utf8, wchar_t *out, int out_count)
{
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, out_count) > 0;
}

static bool wide_to_utf8(const wchar_t *wide, char *out, int out_size)
{
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, out_size, NULL, NULL) > 0;
}

static void scan_folder(const char *relative, int depth)
{
    char pattern[VFS_MAX_PATH];
    wchar_t wide_pattern[VFS_MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE handle;

    if (depth > VFS_MAX_DEPTH)
        return;

    if (relative[0] == '\0')
        snprintf(pattern, sizeof(pattern), "%s\\*", g_vfs.root);
    else
        snprintf(pattern, sizeof(pattern), "%s\\%s\\*", g_vfs.root, relative);
    if (!utf8_to_wide(pattern, wide_pattern, VFS_MAX_PATH))
        return;

    handle = FindFirstFileW(wide_pattern, &found);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    do {
        char name[VFS_MAX_PATH];
        bool is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (wide_to_utf8(found.cFileName, name, sizeof(name)))
            scan_child(relative, name, is_dir, depth);
    } while (FindNextFileW(handle, &found));
    FindClose(handle);
}

static FILE *open_file(const char *path)
{
    wchar_t wide_path[VFS_MAX_PATH];
    if (!utf8_to_wide(path, wide_path, VFS_MAX_PATH))
        return NULL;
    return _wfopen(wide_path, L"rb");
}

#else

static void scan_folder(const char *relative, int depth)
{
    char folder[VFS_MAX_PATH];
    DIR *dir;
    struct dirent *item;

    if (depth > VFS_MAX_DEPTH)
        return;

    join_path(folder, sizeof(folder), g_vfs.root, relative);
    dir = opendir(folder);
    if (dir == NULL)
        return;

    while ((item = readdir(dir)) != NULL) {
        char full[VFS_MAX_PATH];
        struct stat info;

        join_path(full, sizeof(full), folder, item->d_name);
        if (stat(full, &info) != 0)
            continue;
        scan_child(relative, item->d_name, S_ISDIR(info.st_mode), depth);
    }
    closedir(dir);
}

static FILE *open_file(const char *path)
{
    return fopen(path, "rb");
}

#endif

static int compare_entries(const void *a, const void *b)
{
    const VfsEntry *ea = a;
    const VfsEntry *eb = b;
    return strcmp(ea->key, eb->key);
}

/* The same, with the OpenMB2 folder's copy of a path before the game's. */
static int compare_layers(const void *a, const void *b)
{
    const VfsEntry *ea = a;
    const VfsEntry *eb = b;
    int order = strcmp(ea->key, eb->key);
    return order != 0 ? order : (int)eb->added - (int)ea->added;
}

bool vfs_mount(const char *root_dir)
{
    int i, kept;

    vfs_unmount();
    g_vfs.root = copy_string(root_dir);

    /* Strip a trailing slash so joined paths look tidy. */
    {
        size_t len = strlen(g_vfs.root);
        while (len > 1 && (g_vfs.root[len - 1] == '/' || g_vfs.root[len - 1] == '\\'))
            g_vfs.root[--len] = '\0';
    }

    scan_folder("", 0);
    if (g_vfs.count == 0) {
        log_error("vfs: '%s' is empty or can't be read", root_dir);
        vfs_unmount();
        return false;
    }

    qsort(g_vfs.entries, (size_t)g_vfs.count, sizeof(VfsEntry), compare_layers);

    /*
     * On case-sensitive systems two files can differ only by case
     * ("Menu/Exit.txt" and "Menu/EXIT.TXT"). Windows would only ever see
     * one of them, so keep the first and drop the rest. A path can still
     * be there twice: the OpenMB2 folder's copy first, then the game's.
     */
    kept = 0;
    for (i = 0; i < g_vfs.count; i++) {
        VfsEntry *entry = &g_vfs.entries[i];
        if (kept > 0 && strcmp(g_vfs.entries[kept - 1].key, entry->key) == 0 &&
            g_vfs.entries[kept - 1].added == entry->added) {
            log_warn("vfs: ignoring '%s' (same name as '%s')",
                     entry->real, g_vfs.entries[kept - 1].real);
            if (!entry->is_dir)
                g_vfs.file_count--;
            free(entry->key);
            free(entry->real);
            continue;
        }
        g_vfs.entries[kept++] = *entry;
    }
    g_vfs.count = kept;

    log_info("vfs: mounted '%s' (%d files)", g_vfs.root, g_vfs.file_count);
    return true;
}

void vfs_unmount(void)
{
    int i;
    for (i = 0; i < g_vfs.count; i++) {
        free(g_vfs.entries[i].key);
        free(g_vfs.entries[i].real);
    }
    free(g_vfs.entries);
    free(g_vfs.root);
    memset(&g_vfs, 0, sizeof(g_vfs));
}

/* ------------------------------------------------------------------------ */
/* Lookups                                                                  */
/* ------------------------------------------------------------------------ */

/* The OpenMB2 folder's copy of a path if there is one, else the game's. */
static const VfsEntry *find_entry(const char *game_path)
{
    char key[VFS_MAX_PATH];
    VfsEntry probe;
    const VfsEntry *entry;

    if (g_vfs.count == 0)
        return NULL;

    vfs_normalise(game_path, key, sizeof(key));
    probe.key = key;
    entry = bsearch(&probe, g_vfs.entries, (size_t)g_vfs.count,
                    sizeof(VfsEntry), compare_entries);
    while (entry != NULL && entry > g_vfs.entries && strcmp(entry[-1].key, key) == 0)
        entry--;
    return entry;
}

/* The game's own copy of a path, never the OpenMB2 folder's. */
static const VfsEntry *find_game_entry(const char *game_path)
{
    const VfsEntry *entry = find_entry(game_path);

    if (entry != NULL && entry->added) {
        const VfsEntry *next = entry + 1;
        entry = next < g_vfs.entries + g_vfs.count && strcmp(next->key, entry->key) == 0 ? next : NULL;
    }
    return entry;
}

const char *vfs_root(void)
{
    return g_vfs.root;
}

int vfs_file_count(void)
{
    return g_vfs.file_count;
}

bool vfs_exists(const char *game_path)
{
    const VfsEntry *entry = find_entry(game_path);
    return entry != NULL && !entry->is_dir;
}

bool vfs_is_dir(const char *game_path)
{
    const VfsEntry *entry = find_entry(game_path);
    return entry != NULL && entry->is_dir;
}

static bool real_path(const VfsEntry *entry, char *out, size_t out_size)
{
    int written;

    if (entry == NULL)
        return false;
    written = snprintf(out, out_size, "%s/%s", g_vfs.root, entry->real);
    return written >= 0 && (size_t)written < out_size;
}

bool vfs_real_path(const char *game_path, char *out, size_t out_size)
{
    return real_path(find_entry(game_path), out, out_size);
}

bool vfs_game_path(const char *game_path, char *out, size_t out_size)
{
    return real_path(find_game_entry(game_path), out, out_size);
}

bool vfs_is_added(const char *game_path)
{
    const VfsEntry *entry = find_entry(game_path);
    return entry != NULL && entry->added && find_game_entry(game_path) == NULL;
}

void *vfs_read(const char *game_path, size_t *out_size)
{
    char path[VFS_MAX_PATH];
    FILE *file;
    long length;
    char *data;

    if (out_size != NULL)
        *out_size = 0;
    if (!vfs_exists(game_path) || !vfs_real_path(game_path, path, sizeof(path)))
        return NULL;

    file = open_file(path);
    if (file == NULL)
        return NULL;

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0) {
        fclose(file);
        return NULL;
    }

    data = malloc((size_t)length + 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);

    data[length] = '\0';
    if (out_size != NULL)
        *out_size = (size_t)length;
    return data;
}

int vfs_list(const char *game_dir, VfsListFn fn, void *user)
{
    char prefix[VFS_MAX_PATH];
    size_t prefix_len;
    int i, visited = 0;

    vfs_normalise(game_dir, prefix, sizeof(prefix));
    prefix_len = strlen(prefix);

    for (i = 0; i < g_vfs.count; i++) {
        const VfsEntry *entry = &g_vfs.entries[i];
        const char *rest = entry->key;
        const char *name;

        if (prefix_len > 0) {
            if (strncmp(entry->key, prefix, prefix_len) != 0 || entry->key[prefix_len] != '/')
                continue;
            rest = entry->key + prefix_len + 1;
        }
        if (strchr(rest, '/') != NULL)
            continue; /* deeper than a direct child */
        if (i > 0 && strcmp(g_vfs.entries[i - 1].key, entry->key) == 0)
            continue; /* the game's copy of one the OpenMB2 folder has */

        /* Report the real name, which is the last part of the real path. */
        name = strrchr(entry->real, '/');
        name = name ? name + 1 : entry->real;
        fn(name, entry->is_dir, user);
        visited++;
    }
    return visited;
}
