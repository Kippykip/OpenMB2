# OpenMB2

An open reimplementation of the Magic Ball 2 / Magic Ball 2: New Worlds
engine, written from scratch in plain C so it can run anywhere SDL2 and
OpenGL ES 2 run: Windows, Linux, ARM handhelds, Android/iOS and web
browsers.

OpenMB2 contains **no game data**. It loads the files from your own copy of
the game, so you need a Magic Ball 2 install with loose files (for example
the Community Edition or one of the unboxed builds).

## Status

Milestone 1 of 6: the engine skeleton.

- [x] Reads the game folder, with Windows-style file names on any system
- [x] Parses the game's config files (menus, boards, pads, prizes, ...)
- [x] Reads level files and `params.txt`
- [x] Opens a window with an OpenGL ES 2 (or OpenGL 2.1) context
- [ ] Milestone 2: draw a level in 3D (meshes, textures, camera)
- [ ] Milestone 3: playable core (bat, ball, bricks, score, sound, music)
- [ ] Milestone 4: prizes, cannons, teleports, water/fire, animals
- [ ] Milestone 5: menus, fonts, languages, options, saves
- [ ] Milestone 6: web, Android and handheld builds, online scores

## Running

```
openmb2 --game "G:\Games\Magic Ball 2 Community Edition"
```

or copy `openmb2.exe` into the game folder and run it from there. For now
it opens a window, logs what it found, and quits with Escape.
Alt+Enter toggles fullscreen.

`mb2inspect` looks inside a game folder:

```
mb2inspect <game folder> scan            check every config and level
mb2inspect <game folder> config <file>   print a config file
mb2inspect <game folder> level <file>    print a level's objects
mb2inspect <game folder> ls [folder]     list a folder
```

## Building

You need CMake 3.16+ and a C compiler. SDL2 is downloaded and built
automatically if it isn't installed (turn that off with
`-DOPENMB2_FETCH_SDL=OFF`).

```
cmake -S . -B build
cmake --build build
```

- **Windows (Visual Studio):** open the folder in Visual Studio, or run the
  commands above from a Developer Command Prompt.
- **Windows from Linux:** add
  `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake` (needs MinGW-w64).
- **Linux:** install `libsdl2-dev` first to skip the download.
- **Web:** `emcmake cmake -S . -B build-web && cmake --build build-web`
  (game files will need to be packaged or uploaded; that comes in
  milestone 6).

## Source layout

```
src/core/      no SDL or GL here; shared with the tools
  vfs.c        case-insensitive access to the game folder
  config.c     parser for the game's { key: value } files
  log.c        logging
src/game/      game data and (later) game logic
  level.c      .mb2 level loader
  params.c     params.txt settings
src/platform/  SDL and OpenGL glue
  gl.c         loads GL functions at runtime
src/main.c     startup and main loop
tools/         command-line tools
docs/          file format notes
```

The code aims to be easy to read: one job per file, plain C99, comments
that say *why*. See `docs/formats.md` for what's known about the data files.

## Licence

OpenMB2 is free software under the GNU General Public License, version 2 or
(at your option) any later version. See `LICENSE`.

This licence covers the OpenMB2 code only. Magic Ball 2's own files (levels,
models, textures, sounds, music) belong to their original owners and are not
part of this project.
