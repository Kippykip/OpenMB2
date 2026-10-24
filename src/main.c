/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * main.c - OpenMB2 entry point.
 *
 * Finds the user's game folder, reads their settings, opens a window with
 * an OpenGL ES 2 context and runs the main loop. For now (milestone 3)
 * the loop goes straight to playing a level, see play_screen.h.
 *
 * Usage: openmb2 [--game <folder>] [--level <file>] [--debug]
 *                [--screenshot <file.bmp>] [--run-ms <ms>] [--autoplay]
 *
 *   --level       start on this level, e.g. "Levels1/001_Vesi.mb2"
 *   --screenshot  save a frame as a .bmp and quit (for testing); with
 *                 --run-ms, after playing that long in steady 18 ms frames
 *   --autoplay    the bat follows the ball by itself (for testing)
 */
#include "audio/audio.h"
#include "core/config.h"
#include "core/log.h"
#include "core/vfs.h"
#include "game/level.h"
#include "game/params.h"
#include "platform/gl.h"
#include "play_screen.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    const char *screenshot_path;
    int run_ms;          /* with screenshot_path: game time to play first */
    uint32_t last_ticks;
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

/* Save the frame just drawn (before swapping) as a .bmp. */
static void save_screenshot(const char *path, int width, int height)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                          SDL_PIXELFORMAT_ABGR8888);
    int y;

    if (surface == NULL)
        return;
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
    /* GL rows go bottom to top; flip them. */
    for (y = 0; y < height / 2; y++) {
        Uint8 *a = (Uint8 *)surface->pixels + (size_t)y * surface->pitch;
        Uint8 *b = (Uint8 *)surface->pixels + (size_t)(height - 1 - y) * surface->pitch;
        int x;
        for (x = 0; x < surface->pitch; x++) {
            Uint8 t = a[x];
            a[x] = b[x];
            b[x] = t;
        }
    }
    if (SDL_SaveBMP(surface, path) == 0)
        log_info("saved %s", path);
    else
        log_error("can't save %s: %s", path, SDL_GetError());
    SDL_FreeSurface(surface);
}

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
        else
            play_screen_handle_event(event);
        break;
    case SDL_WINDOWEVENT:
        /* The bat takes the mouse while the window has focus. */
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            SDL_SetRelativeMouseMode(SDL_TRUE);
        else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            SDL_SetRelativeMouseMode(SDL_FALSE);
        break;
    default:
        play_screen_handle_event(event);
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
    if (g_app.screenshot_path != NULL) {
        /* Testing: play in steady steps, then save the last frame. */
        int frames = g_app.run_ms / 18, i;
        for (i = 0; i < frames; i++)
            play_screen_frame(width, height, 18);
        play_screen_frame(width, height, 0);
        save_screenshot(g_app.screenshot_path, width, height);
        g_app.running = false;
    } else {
        Uint32 now = SDL_GetTicks();
        play_screen_frame(width, height, now - g_app.last_ticks);
        g_app.last_ticks = now;
    }
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
    play_screen_shutdown();
    audio_shutdown();
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
    PlayScreenOptions options;
    int i;

    memset(&options, 0, sizeof(options));
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game_folder = argv[++i];
        else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc)
            options.start_level = argv[++i];
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            g_app.screenshot_path = argv[++i];
        else if (strcmp(argv[i], "--run-ms") == 0 && i + 1 < argc)
            g_app.run_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--autoplay") == 0)
            options.autoplay = true;
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

    srand((unsigned)time(NULL));
    audio_init(g_app.params.sound_volume, g_app.params.music_volume);
    options.mouse_sensitivity = g_app.params.mouse_sensitivity;
    options.infravision = g_app.params.infravision;
    if (!play_screen_init(g_app.window, &options)) {
        show_error("Couldn't start the game. See the log for details.");
        shutdown_app();
        return 1;
    }

    g_app.running = true;
    g_app.last_ticks = SDL_GetTicks();
    if (g_app.screenshot_path == NULL)
        SDL_SetRelativeMouseMode(SDL_TRUE);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(run_frame, 0, 1);
#else
    while (g_app.running)
        run_frame();
#endif

    shutdown_app();
    return 0;
}
