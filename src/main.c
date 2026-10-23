/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * main.c - OpenMB2 entry point.
 *
 * Milestone 1: find the user's game folder, read their settings, open a
 * window with an OpenGL ES 2 context and run the main loop. Nothing is
 * drawn yet apart from a clear colour; the loaders are exercised at
 * startup so problems show up in the log.
 *
 * Usage: openmb2 [--game <folder>] [--debug]
 */
#include "core/config.h"
#include "core/log.h"
#include "core/vfs.h"
#include "game/level.h"
#include "game/params.h"
#include "platform/gl.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* The original game's menus are laid out for 800x600. */
#define DEFAULT_WIDTH  800
#define DEFAULT_HEIGHT 600

/* Every release has this file in the root of the game folder. */
#define GAME_FOLDER_MARKER "textureset.txt"

typedef struct App {
    SDL_Window *window;
    SDL_GLContext gl_context;
    Params params;
    bool running;
} App;

static App g_app;

/* ------------------------------------------------------------------------ */
/* Finding the game folder                                                  */
/* ------------------------------------------------------------------------ */

static bool try_game_folder(const char *folder)
{
    if (folder == NULL || folder[0] == '\0')
        return false;
    if (!vfs_mount(folder))
        return false;
    if (vfs_exists(GAME_FOLDER_MARKER))
        return true;

    log_info("'%s' doesn't look like a Magic Ball 2 folder (no %s)", folder, GAME_FOLDER_MARKER);
    vfs_unmount();
    return false;
}

/*
 * Look for the game data in this order: the --game argument, the current
 * folder, then the folder the executable is in.
 */
static bool find_game_folder(const char *from_args)
{
    char *base_path;
    bool found;

    if (from_args != NULL)
        return try_game_folder(from_args);
    if (try_game_folder("."))
        return true;

    base_path = SDL_GetBasePath();
    found = try_game_folder(base_path);
    SDL_free(base_path);
    return found;
}

/* ------------------------------------------------------------------------ */
/* Startup checks                                                           */
/* ------------------------------------------------------------------------ */

typedef struct LevelFolderScan {
    int level_count;
    char first_level[256];
} LevelFolderScan;

static void count_level(const char *name, bool is_dir, void *user)
{
    LevelFolderScan *scan = user;
    size_t len = strlen(name);

    if (is_dir || len < 4 || SDL_strcasecmp(name + len - 4, ".mb2") != 0)
        return;
    if (scan->level_count == 0 || strcmp(name, scan->first_level) < 0)
        snprintf(scan->first_level, sizeof(scan->first_level), "%s", name);
    scan->level_count++;
}

/*
 * Report what the game folder contains. The original engine looks for
 * level packs in "Levels", "Levels1".."Levels7" and "Levels0r".
 */
static void log_game_contents(void)
{
    static const char *const pack_folders[] = {
        "Levels", "Levels1", "Levels2", "Levels3", "Levels4",
        "Levels5", "Levels6", "Levels7", "Levels0r"
    };
    size_t i;

    for (i = 0; i < sizeof(pack_folders) / sizeof(pack_folders[0]); i++) {
        LevelFolderScan scan = { 0, "" };
        char path[300];
        Level level;

        if (!vfs_is_dir(pack_folders[i]))
            continue;
        vfs_list(pack_folders[i], count_level, &scan);
        if (scan.level_count == 0)
            continue;

        /* Load the first level as a smoke test of the level loader. */
        snprintf(path, sizeof(path), "%s/%s", pack_folders[i], scan.first_level);
        if (level_load(&level, path)) {
            log_info("pack %-9s %3d levels, first '%s' has %d objects on %s",
                     pack_folders[i], scan.level_count, scan.first_level,
                     level.object_count, level.slots[LEVEL_SLOT_BOARD]);
            level_free(&level);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Window and GL context                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Ask for an OpenGL ES 2 context first, since that's what handhelds,
 * phones and browsers have. Desktop drivers without ES support get a
 * plain OpenGL 2.1 context, which covers the same feature set.
 */
static bool create_gl_context(void)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    g_app.gl_context = SDL_GL_CreateContext(g_app.window);

    if (g_app.gl_context == NULL) {
        log_info("no OpenGL ES 2 context (%s), trying OpenGL 2.1", SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        g_app.gl_context = SDL_GL_CreateContext(g_app.window);
    }
    if (g_app.gl_context == NULL) {
        log_error("can't create an OpenGL context: %s", SDL_GetError());
        return false;
    }

    if (!gl_load_functions())
        return false;
    SDL_GL_SetSwapInterval(1);
    log_info("renderer: %s (%s)", (const char *)gl.GetString(GL_RENDERER),
             (const char *)gl.GetString(GL_VERSION));
    return true;
}

static bool create_window(void)
{
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    if (!g_app.params.windowed)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    g_app.window = SDL_CreateWindow("OpenMB2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    DEFAULT_WIDTH, DEFAULT_HEIGHT, flags);
    if (g_app.window == NULL) {
        log_error("can't create window: %s", SDL_GetError());
        return false;
    }
    return create_gl_context();
}

static void toggle_fullscreen(void)
{
    Uint32 flags = SDL_GetWindowFlags(g_app.window);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(g_app.window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

/* ------------------------------------------------------------------------ */
/* Main loop                                                                */
/* ------------------------------------------------------------------------ */

static void handle_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_QUIT:
        g_app.running = false;
        break;
    case SDL_KEYDOWN:
        if (event->key.keysym.sym == SDLK_ESCAPE)
            g_app.running = false;
        else if (event->key.keysym.sym == SDLK_RETURN && (event->key.keysym.mod & KMOD_ALT))
            toggle_fullscreen();
        break;
    default:
        break;
    }
}

static void run_frame(void)
{
    SDL_Event event;
    int width, height;

    while (SDL_PollEvent(&event))
        handle_event(&event);

    SDL_GL_GetDrawableSize(g_app.window, &width, &height);
    gl.Viewport(0, 0, width, height);
    gl.ClearColor(0.05f, 0.08f, 0.20f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(g_app.window);

#ifdef __EMSCRIPTEN__
    if (!g_app.running)
        emscripten_cancel_main_loop();
#endif
}

/* ------------------------------------------------------------------------ */

static void show_error(const char *message)
{
    log_error("%s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenMB2", message, NULL);
}

static void shutdown_app(void)
{
    if (g_app.gl_context != NULL)
        SDL_GL_DeleteContext(g_app.gl_context);
    if (g_app.window != NULL)
        SDL_DestroyWindow(g_app.window);
    vfs_unmount();
    SDL_Quit();
}

int main(int argc, char **argv)
{
    const char *game_folder = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game_folder = argv[++i];
        else if (strcmp(argv[i], "--debug") == 0)
            log_set_level(LOG_DEBUG);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        log_error("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (!find_game_folder(game_folder)) {
        show_error("Magic Ball 2 game files not found.\n\n"
                   "Run OpenMB2 from your Magic Ball 2 folder, or start it with\n"
                   "--game \"path to your Magic Ball 2 folder\".");
        shutdown_app();
        return 1;
    }

    if (!params_load(&g_app.params, "params.txt"))
        log_info("no params.txt, using default settings");
    log_game_contents();

    if (!create_window()) {
        show_error("Couldn't open a window with OpenGL support.");
        shutdown_app();
        return 1;
    }

    g_app.running = true;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(run_frame, 0, 1);
#else
    while (g_app.running)
        run_frame();
#endif

    shutdown_app();
    return 0;
}
