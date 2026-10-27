/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "userfile.h"
#include "core/log.h"
#include "core/vfs.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* In a browser the game folder is read-only and gone on reload, so the
   files are written to /save, which the page keeps in the browser's
   storage (IndexedDB). Reading falls back to the game folder's copy. */
#define SAVE_DIR "/save"

static bool save_path(const char *name, char *out, size_t size)
{
    int n = snprintf(out, size, SAVE_DIR "/%s", name);
    return n > 0 && (size_t)n < size;
}
#endif

void userfile_path(const char *name, char *out, size_t size)
{
    const char *root = vfs_root();
    size_t n = root != NULL ? strlen(root) : 0;

#ifdef __EMSCRIPTEN__
    if (save_path(name, out, size))
        return;
#endif
    if (vfs_game_path(name, out, size))
        return;
    snprintf(out, size, "%s%s%s", root != NULL ? root : ".",
             n > 0 && (root[n - 1] == '/' || root[n - 1] == '\\') ? "" : "/", name);
}

unsigned char *userfile_read(const char *name, size_t *size)
{
    char path[1024];
    SDL_RWops *rw;
    Sint64 length;
    unsigned char *data;

    *size = 0;
    userfile_path(name, path, sizeof(path));
    rw = SDL_RWFromFile(path, "rb");
#ifdef __EMSCRIPTEN__
    if (rw == NULL && vfs_game_path(name, path, sizeof(path)))
        rw = SDL_RWFromFile(path, "rb");
#endif
    if (rw == NULL)
        return NULL;
    length = SDL_RWsize(rw);
    data = length > 0 ? malloc((size_t)length) : NULL;
    if (data != NULL && SDL_RWread(rw, data, 1, (size_t)length) != (size_t)length) {
        free(data);
        data = NULL;
    }
    SDL_RWclose(rw);
    if (data != NULL)
        *size = (size_t)length;
    return data;
}

bool userfile_write(const char *name, const void *data, size_t size)
{
    char path[1024];
    SDL_RWops *rw;
    bool ok;

    userfile_path(name, path, sizeof(path));
    rw = SDL_RWFromFile(path, "wb");
    if (rw == NULL) {
        log_warn("can't write %s: %s", path, SDL_GetError());
        return false;
    }
    ok = SDL_RWwrite(rw, data, 1, size) == size;
    SDL_RWclose(rw);
#ifdef __EMSCRIPTEN__
    /* Copy /save to the browser's storage now: a page can close at any time. */
    EM_ASM(if (typeof FS !== 'undefined' && FS.syncfs) FS.syncfs(false, function () {}););
#endif
    return ok;
}
