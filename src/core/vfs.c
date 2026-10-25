/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "vfs.h"
#include "log.h"
#include "molebox.h"

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
    bool outside; /* in the overrides folder, not the game folder */
    int box;     /* the MoleBox it is in, or -1 for a file on disk */
    int item;    /* its number in that box */
} VfsEntry;

/* The folder the OpenMB2 folder is in, when not the game folder. */
static char *g_overrides;

/* The folder being scanned, and whether it is the overrides folder. */
static const char *g_scan_base;
static bool g_scan_outside;

static struct {
    char *root;
    char *overrides; /* NULL: the OpenMB2 folder is in the game folder */
    VfsEntry *entries;
    int count;
    int capacity;
    int file_count;
    MoleBox **boxes;
    int box_count;
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
    entry->outside = g_scan_outside;
    entry->box = -1;
    entry->item = 0;
    if (!is_dir && !added)
        g_vfs.file_count++;
}

static void add_entry(const char *relative, bool is_dir)
{
    char key[VFS_MAX_PATH];
    size_t added = strlen(VFS_ADDED);

    vfs_normalise(relative, key, sizeof(key));
    if (!g_scan_outside)
        push_entry(key, relative, is_dir, false);
    /* A file under OpenMB2/ is also listed under its path without it. */
    if (strncmp(key, VFS_ADDED, added) == 0 && key[added] != '\0' &&
        (g_vfs.overrides == NULL) == !g_scan_outside)
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
        snprintf(pattern, sizeof(pattern), "%s\\*", g_scan_base);
    else
        snprintf(pattern, sizeof(pattern), "%s\\%s\\*", g_scan_base, relative);
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

    join_path(folder, sizeof(folder), g_scan_base, relative);
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

/* ------------------------------------------------------------------------ */
/* MoleBox boxes                                                            */
/* ------------------------------------------------------------------------ */

static bool has_extension(const char *key, const char *ext)
{
    size_t len = strlen(key), ext_len = strlen(ext);
    return len > ext_len && strcmp(key + len - ext_len, ext) == 0;
}

/* List a box's files, and the folders they are in, under their paths. */
static void add_box(MoleBox *box)
{
    int index = g_vfs.box_count, i;
    MoleBox **grown = realloc(g_vfs.boxes, (size_t)(index + 1) * sizeof(*grown));

    if (grown == NULL) {
        molebox_close(box);
        return;
    }
    g_vfs.boxes = grown;
    g_vfs.boxes[g_vfs.box_count++] = box;

    for (i = 0; i < molebox_count(box); i++) {
        char key[VFS_MAX_PATH], *slash;
        const char *name = molebox_name(box, i);
        int first = g_vfs.count;

        vfs_normalise(name, key, sizeof(key));
        push_entry(key, name, false, false);
        /* Its folders, from the innermost out; duplicates go at sorting. */
        while ((slash = strrchr(key, '/')) != NULL) {
            char real[VFS_MAX_PATH];
            *slash = '\0';
            snprintf(real, sizeof(real), "%.*s", (int)(slash - key), name);
            push_entry(key, real, true, false);
        }
        while (first < g_vfs.count) {
            g_vfs.entries[first].box = index;
            g_vfs.entries[first++].item = i;
        }
    }
}

/*
 * Most releases keep their data in MoleBox boxes (*.pak, or the disc's
 * MagicBall2.dat) that the original exe's runtime shows the game as
 * folders; read them the same way. Files on disk come first, so a loose
 * copy can stand in for a box's.
 */
static void mount_boxes(void)
{
    int i, count = g_vfs.count;

    for (i = 0; i < count; i++) {
        const VfsEntry *entry = &g_vfs.entries[i];
        char path[VFS_MAX_PATH];
        MoleBox *box;

        if (entry->is_dir || entry->added || entry->outside || strchr(entry->key, '/') != NULL ||
            !(has_extension(entry->key, ".pak") || has_extension(entry->key, ".dat")))
            continue;
        snprintf(path, sizeof(path), "%s/%s", g_vfs.root, entry->real);
        box = molebox_open(path);
        if (box != NULL)
            add_box(box);
    }
}

/* Which copy of a path wins: the OpenMB2 folder's, then the game folder's,
   then a box's. */
static int layer(const VfsEntry *entry)
{
    return entry->added ? 0 : entry->box < 0 ? 1 : 2;
}

static int compare_entries(const void *a, const void *b)
{
    const VfsEntry *ea = a;
    const VfsEntry *eb = b;
    return strcmp(ea->key, eb->key);
}

/* The same, with the copies of a path in the order they win. */
static int compare_layers(const void *a, const void *b)
{
    const VfsEntry *ea = a;
    const VfsEntry *eb = b;
    int order = strcmp(ea->key, eb->key);
    return order != 0 ? order : layer(ea) - layer(eb);
}

/* A copy without the trailing slash, so joined paths look tidy. */
static char *copy_folder(const char *dir)
{
    char *copy = copy_string(dir);
    size_t len = copy != NULL ? strlen(copy) : 0;

    while (len > 1 && (copy[len - 1] == '/' || copy[len - 1] == '\\'))
        copy[--len] = '\0';
    return copy;
}

void vfs_set_overrides(const char *dir)
{
    free(g_overrides);
    g_overrides = dir != NULL && dir[0] != '\0' ? copy_folder(dir) : NULL;
}

bool vfs_mount(const char *root_dir)
{
    int i, kept;
    bool found;

    vfs_unmount();
    g_vfs.root = copy_folder(root_dir);
    g_vfs.overrides = g_overrides != NULL ? copy_string(g_overrides) : NULL;

    g_scan_base = g_vfs.root;
    g_scan_outside = false;
    scan_folder("", 0);
    mount_boxes();
    found = g_vfs.count > 0;
    if (g_vfs.overrides != NULL) {
        g_scan_base = g_vfs.overrides;
        g_scan_outside = true;
        i = g_vfs.count;
        scan_folder("OpenMB2", 0);
        if (g_vfs.count == i)
            scan_folder("openmb2", 0);
        g_scan_outside = false;
        log_info("vfs: OpenMB2 folder from '%s' (%d entries)", g_vfs.overrides, g_vfs.count - i);
    }
    if (!found) {
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
     * A box's copy of a path the game folder has is never read, and its
     * folders are there once for every file in them.
     */
    kept = 0;
    for (i = 0; i < g_vfs.count; i++) {
        VfsEntry *entry = &g_vfs.entries[i];
        const VfsEntry *last = kept > 0 ? &g_vfs.entries[kept - 1] : NULL;
        if (last != NULL && strcmp(last->key, entry->key) == 0 &&
            (layer(last) == layer(entry) || (entry->box >= 0 && !last->added))) {
            if (entry->box < 0)
                log_warn("vfs: ignoring '%s' (same name as '%s')", entry->real, last->real);
            if (!entry->is_dir)
                g_vfs.file_count--;
            free(entry->key);
            free(entry->real);
            continue;
        }
        g_vfs.entries[kept++] = *entry;
    }
    g_vfs.count = kept;

    if (g_vfs.box_count > 0)
        log_info("vfs: mounted '%s' (%d files, %d MoleBox boxes)", g_vfs.root, g_vfs.file_count,
                 g_vfs.box_count);
    else
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
    for (i = 0; i < g_vfs.box_count; i++)
        molebox_close(g_vfs.boxes[i]);
    free(g_vfs.boxes);
    free(g_vfs.entries);
    free(g_vfs.root);
    free(g_vfs.overrides);
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

    if (entry == NULL || entry->box >= 0)
        return false;
    written = snprintf(out, out_size, "%s/%s", entry->outside ? g_vfs.overrides : g_vfs.root, entry->real);
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
    const VfsEntry *entry = find_entry(game_path);
    char path[VFS_MAX_PATH];
    FILE *file;
    long length;
    char *data;

    if (out_size != NULL)
        *out_size = 0;
    if (entry == NULL || entry->is_dir)
        return NULL;
    if (entry->box >= 0)
        return molebox_read(g_vfs.boxes[entry->box], entry->item, out_size);
    if (!real_path(entry, path, sizeof(path)))
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
