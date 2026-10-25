/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * main.c - OpenMB2 entry point.
 *
 * Finds the user's game folder, reads their settings, opens a window with
 * an OpenGL ES 2 context and runs the main loop, which shows the menus
 * (menu_screen.h) or a level (play_screen.h).
 *
 * Usage: openmb2 [--game <folder>] [--level <file>] [--debug]
 *                [--screenshot <file.bmp>] [--run-ms <ms>] [--autoplay]
 *
 *   --level       start on this level, e.g. "Levels1/001_Vesi.mb2",
 *                 instead of the splash screen and menus
 *   --menu <list> start on this menu page, e.g. "Menu/loptions.txt"
 *                 (testing)
 *   --screenshot  save a frame as a .bmp and quit (for testing); with
 *                 --run-ms, after playing that long in steady 18 ms frames
 *   --autoplay    the bat follows the ball by itself (for testing)
 *   --wide-menus  fill the screen with the menus instead of keeping them
 *                 4:3 between black bars
 */
#include "audio/audio.h"
#include "core/config.h"
#include "core/log.h"
#include "core/vfs.h"
#include "game/lang.h"
#include "game/level.h"
#include "game/params.h"
#include "game/settings.h"
#include "menu_screen.h"
#include "platform/gl.h"
#include "play_screen.h"
#include "render/render.h"
#include "userfile.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* The original game's menus are laid out for 800x600. */

/* Every release has this file in the root of the game folder. */
#define GAME_FOLDER_MARKER "textureset.txt"

typedef struct App {
    SDL_Window *window;
    SDL_GLContext gl_context;
    Params params;
    Settings settings;   /* openmb2.ini */
    bool running;
    const char *screenshot_path;
    int run_ms;          /* with screenshot_path: game time to play first */
    uint32_t last_ticks;
    bool playing;        /* a level, not the menus */
    bool focused;        /* the window has the keyboard */
    int mouse_mode;      /* what update_mouse_mode() last set, -1 for nothing yet */
    bool resized;        /* the window changed size since the last frame */
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
    SDL_GL_SetSwapInterval(g_app.settings.vsync ? 1 : 0);
    log_info("renderer: %s (%s)", (const char *)gl.GetString(GL_RENDERER),
             (const char *)gl.GetString(GL_VERSION));
    return true;
}

/* The screen size openmb2.ini asks for, or params.txt's video mode. */
static void video_size(int *width, int *height)
{
    if (g_app.settings.width > 0 && g_app.settings.height > 0) {
        *width = g_app.settings.width;
        *height = g_app.settings.height;
    } else {
        params_video_mode(g_app.params.resolution, width, height);
    }
}

static bool want_fullscreen(void)
{
    if (g_app.settings.fullscreen == FULLSCREEN_AUTO)
        return !g_app.params.windowed;
    return g_app.settings.fullscreen == FULLSCREEN_ON;
}

/*
 * Full screen takes the screen mode of the size openmb2.ini gives, or
 * keeps the desktop's when it gives none; in a window, the size is the
 * window's.
 */
static void apply_video(bool fullscreen)
{
    int width, height;

    video_size(&width, &height);
    if (fullscreen && g_app.settings.width > 0 && g_app.settings.height > 0) {
        SDL_DisplayMode want, mode;
        SDL_zero(want);
        want.w = width;
        want.h = height;
        if (SDL_GetClosestDisplayMode(SDL_GetWindowDisplayIndex(g_app.window), &want, &mode) != NULL) {
            SDL_SetWindowFullscreen(g_app.window, 0);
            SDL_SetWindowDisplayMode(g_app.window, &mode);
            SDL_SetWindowFullscreen(g_app.window, SDL_WINDOW_FULLSCREEN);
            return;
        }
    }
    if (fullscreen) {
        SDL_SetWindowFullscreen(g_app.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    SDL_SetWindowFullscreen(g_app.window, 0);
    SDL_RestoreWindow(g_app.window);
    SDL_SetWindowSize(g_app.window, width, height);
    SDL_SetWindowPosition(g_app.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

static bool is_fullscreen(void)
{
    return (SDL_GetWindowFlags(g_app.window) & SDL_WINDOW_FULLSCREEN) != 0;
}

/* The options page picked another size. */
static void video_changed(void)
{
    apply_video(is_fullscreen());
}

/*
 * A window the player resizes keeps that size for next time, and
 * whether it is maximised; the size kept is the one it has when neither
 * maximised nor full screen.
 */
static void remember_window(void)
{
    Uint32 flags;
    int width, height, want_width, want_height;

    if (g_app.screenshot_path != NULL)
        return;
    flags = SDL_GetWindowFlags(g_app.window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED))
        return;
    g_app.settings.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    /* Only a size the window really took, never one still on its way. */
    if (g_app.settings.maximized || !g_app.resized)
        return;
    g_app.resized = false;
    SDL_GetWindowSize(g_app.window, &width, &height);
    video_size(&want_width, &want_height);
    /* Left at the size asked for, 0 x 0 goes on following params.txt. */
    if (width > 0 && height > 0 && (width != want_width || height != want_height)) {
        g_app.settings.width = width;
        g_app.settings.height = height;
    }
}

static bool create_window(void)
{
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int width, height;

    if (g_app.settings.maximized)
        flags |= SDL_WINDOW_MAXIMIZED;
    video_size(&width, &height);
    g_app.window = SDL_CreateWindow("OpenMB2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    width, height, flags);
    if (g_app.window == NULL) {
        log_error("can't create window: %s", SDL_GetError());
        return false;
    }
    if (want_fullscreen())
        apply_video(true);
    return create_gl_context();
}

static void toggle_fullscreen(void)
{
    bool maximized = g_app.settings.maximized;

    apply_video(!is_fullscreen());
    if (!is_fullscreen() && maximized)
        SDL_MaximizeWindow(g_app.window);
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

/*
 * The bat takes the mouse while a level is played; the menus draw their
 * own pointer where the system's is. Paused, the mouse is let go with
 * the system's pointer, which the original keeps hidden and held.
 */
static void update_mouse_mode(void)
{
    bool paused = g_app.playing && play_screen_paused();
    int mode = !g_app.focused ? 0 : g_app.playing && !paused ? 1 : paused ? 2 : 3;

    if (g_app.screenshot_path != NULL || mode == g_app.mouse_mode)
        return;
    g_app.mouse_mode = mode;
    SDL_SetRelativeMouseMode(mode == 1 ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(mode == 1 || mode == 3 ? SDL_DISABLE : SDL_ENABLE);
}

static void set_mouse_mode(bool focused)
{
    g_app.focused = focused;
    update_mouse_mode();
}

static void show_menus(void)
{
    play_screen_stop();
    g_app.playing = false;
    set_mouse_mode(true);
}

static void start_play(void)
{
    const char *folder, *pad;
    int level;

    menu_screen_game(&folder, &level, &pad);
    play_screen_set_options(g_app.params.mouse_sensitivity, g_app.params.infravision);
    if (play_screen_start(folder, level, pad)) {
        g_app.playing = true;
        set_mouse_mode(true);
    }
}

static void handle_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_QUIT:
        g_app.running = false;
        return;
    case SDL_KEYDOWN:
        if (event->key.keysym.sym == SDLK_RETURN && (event->key.keysym.mod & KMOD_ALT)) {
            toggle_fullscreen();
            return;
        }
        if (g_app.playing && event->key.keysym.sym == SDLK_ESCAPE) {
            /* Escape pauses; again, it's the main menu (0x41bee4). */
            if (!play_screen_escape()) {
                show_menus();
                menu_screen_show_main();
            }
            return;
        }
        break;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_RESIZED) {
            g_app.resized = true;
        } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            set_mouse_mode(true);
        } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            set_mouse_mode(false);
            if (g_app.playing)
                play_screen_pause(); /* as the original does when it loses focus */
        }
        return;
    default:
        break;
    }
    if (g_app.playing)
        play_screen_handle_event(event);
    else
        menu_screen_handle_event(event);
}

static void frame(int width, int height, uint32_t ms)
{
    update_mouse_mode();
    remember_window();
    if (g_app.playing) {
        switch (play_screen_frame(width, height, ms)) {
        case PLAY_SCREEN_GAME_OVER: {
            int score = play_screen_score();
            show_menus();
            menu_screen_show_game_over(score);
            break;
        }
        case PLAY_SCREEN_PACK_DONE: {
            int score = play_screen_score();
            show_menus();
            menu_screen_show_end_of_pack(score);
            break;
        }
        default:
            break;
        }
        return;
    }
    switch (menu_screen_frame(width, height, ms)) {
    case MENU_ACTION_PLAY: start_play(); break;
    case MENU_ACTION_QUIT: g_app.running = false; break;
    default: break;
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
        for (i = 0; i < frames && g_app.running; i++)
            frame(width, height, 18);
        frame(width, height, 0);
        save_screenshot(g_app.screenshot_path, width, height);
        g_app.running = false;
    } else {
        Uint32 now = SDL_GetTicks();
        frame(width, height, now - g_app.last_ticks);
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

static void save_params(void)
{
    char text[512];
    int length = params_format(&g_app.params, text, sizeof(text));
    if (length > 0 && length < (int)sizeof(text))
        userfile_write("params.txt", text, (size_t)length);
}

static void save_settings(void)
{
    char text[2048];
    int length = settings_format(&g_app.settings, text, sizeof(text));
    if (length > 0 && length < (int)sizeof(text))
        userfile_write("openmb2.ini", text, (size_t)length);
}

/* openmb2.ini, written with its defaults when there isn't one. */
static void load_settings(void)
{
    size_t size;
    unsigned char *text = userfile_read("openmb2.ini", &size);

    settings_set_defaults(&g_app.settings);
    if (text == NULL) {
        log_info("no openmb2.ini, writing one with the default settings");
        save_settings();
        return;
    }
    settings_parse(&g_app.settings, (const char *)text, size);
    free(text);
}

static void shutdown_app(void)
{
    menu_screen_shutdown();
    play_screen_shutdown();
    lang_free();
    render_shutdown();
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
    const char *game_folder = NULL, *start_level = NULL, *start_menu = NULL;
    PlayScreenOptions options;
    unsigned seed = (unsigned)time(NULL);
    /* Switches given on the command line, which win over openmb2.ini. */
    bool no_smooth = false, no_intro = false, original_glass = false, original_bounce = false,
         original_mouse = false, wide_menus = false;
    int i;

    g_app.mouse_mode = -1;
    memset(&options, 0, sizeof(options));
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game_folder = argv[++i];
        else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc)
            start_level = argv[++i];
        else if (strcmp(argv[i], "--menu") == 0 && i + 1 < argc)
            start_menu = argv[++i];
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            g_app.screenshot_path = argv[++i];
        else if (strcmp(argv[i], "--run-ms") == 0 && i + 1 < argc)
            g_app.run_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-smooth") == 0)
            no_smooth = true;
        else if (strcmp(argv[i], "--no-intro") == 0)
            no_intro = true;
        else if (strcmp(argv[i], "--original-glass") == 0)
            original_glass = true;
        else if (strcmp(argv[i], "--original-bounce") == 0)
            original_bounce = true;
        else if (strcmp(argv[i], "--original-mouse") == 0)
            original_mouse = true;
        else if (strcmp(argv[i], "--wide-menus") == 0)
            wide_menus = true;
        else if (strcmp(argv[i], "--autoplay") == 0)
            options.autoplay = true;
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
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
    load_settings();
    options.smooth = g_app.settings.smooth && !no_smooth;
    options.intro = g_app.settings.level_intro && !no_intro;
    options.sort_glass = g_app.settings.sort_glass && !original_glass;
    options.edge_bounce = g_app.settings.edge_bounce && !original_bounce;
    options.precise_mouse = g_app.settings.precise_mouse && !original_mouse;
    wide_menus = wide_menus || g_app.settings.wide_menus;
    log_game_contents();

    if (!create_window()) {
        show_error("Couldn't open a window with OpenGL support.");
        shutdown_app();
        return 1;
    }

    srand(seed);
    audio_init(g_app.params.sound_volume, g_app.params.music_volume);
    options.mouse_sensitivity = g_app.params.mouse_sensitivity;
    options.infravision = g_app.params.infravision;
    if (!render_init() || !play_screen_init(g_app.window, &options) ||
        !menu_screen_init(g_app.window, &g_app.params, true)) {
        show_error("Couldn't start the game. See the log for details.");
        shutdown_app();
        return 1;
    }
    menu_screen_set_wide(wide_menus);
    menu_screen_set_settings(&g_app.settings, video_changed);
    lang_load(g_app.params.language);

    g_app.running = true;
    g_app.last_ticks = SDL_GetTicks();
    if (start_level != NULL && play_screen_start_file(start_level))
        g_app.playing = true;
    else if (start_menu != NULL)
        menu_screen_show_page(start_menu);
    else
        menu_screen_show_splash();
    set_mouse_mode(true);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(run_frame, 0, 1);
#else
    while (g_app.running)
        run_frame();
#endif

    save_params();
    save_settings();
    shutdown_app();
    return 0;
}
