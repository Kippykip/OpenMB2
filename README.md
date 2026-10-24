# OpenMB2

An open reimplementation of the Magic Ball 2 / Magic Ball 2: New Worlds
engine, written from scratch in plain C so it can run anywhere SDL2 and
OpenGL ES 2 run: Windows, Linux, ARM handhelds, Android/iOS and web
browsers.

OpenMB2 contains **no game data**. It loads the files from your own copy of
the game, so you need a Magic Ball 2 install with loose files (for example
the Community Edition or one of the unboxed builds).

## Status

Milestone 2 of 6: a 3D level viewer.

- [x] Reads the game folder, with Windows-style file names on any system
- [x] Parses the game's config files (menus, boards, pads, prizes, ...)
- [x] Reads level files and `params.txt`
- [x] Opens a window with an OpenGL ES 2 (or OpenGL 2.1) context
- [x] Loads DirectX `.x` models (text and binary) and the game's textures
- [x] Milestone 2: draws any level in 3D with its board, bricks and camera
- [ ] Animated models, the sky background and non-brick objects
- [ ] Milestone 3: playable core (bat, ball, bricks, score, sound, music)
- [ ] Milestone 4: prizes, cannons, teleports, water/fire, animals
- [x] Milestone 5: menus, fonts, languages, options, saves
- [ ] Milestone 6: web, Android and handheld builds, online scores

## Running

```
openmb2 --game "G:\Games\Magic Ball 2 Community Edition"
```

or copy `openmb2.exe` into the game folder and run it from there. It
starts like the original: the splash screen, the Alawar logo (any key
or click skips it), then the main menu, with
the level packs, level pages, options, how to play and high scores. The
settings, high scores (`score.dat`) and opened levels (`s<pack>.sav`)
are the original's own files, so both games share them. In a level:

| Key | Does |
| --- | --- |
| Left / Right | previous / next level |
| Page Up / Page Down | previous / next level pack |
| mouse drag, wheel | turn and zoom the camera |
| Space | reset the camera |
| F10 | smooth motion on / off |
| Alt+Enter | fullscreen |
| Escape | pause; again for the main menu |
| S / L | save / load the game (default.save, shared with the original) |
| Shift+1-3 / 1-3 | save / load the hidden quick slots |
| Shift+F11 | skip the level (a developers' cheat in the original) |

`--level "Levels1/001_Vesi.mb2"` starts on a level instead of the menus,
`--menu Menu/loptions.txt` on a menu page, and
`--screenshot shot.bmp` saves the first frame and quits.

The game itself runs in fixed 18 ms steps, as the original does. On
screens faster than that, OpenMB2 draws the balls, prizes and shots part
way between their last two steps so they move smoothly, and falling,
tipping and shrinking bricks too; the rules are
untouched. `--no-smooth` (or F10) draws them only where each step leaves
them, like the original.

Each level opens with the original's camera fly-in; `--no-intro` skips it.

The original draws bricks texture by texture, so from some angles glass
shows over glass in front of it, or a solid brick behind shows through.
OpenMB2 draws solid bricks first and glass from the back forwards;
`--original-glass` goes back to the original's order.

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
  xfile.c      DirectX .x model loader
  image.c      texture decoding (jpg/png/bmp, DDS)
  matrix.c     Direct3D-style vectors and matrices
  log.c        logging
src/game/      game data and (later) game logic
  level.c      .mb2 level loader
  board.c      board and background files
  textures.c   textureset.txt and texture scripts
  scene.c      builds a level's geometry (bricks baked like the original)
  params.c     params.txt settings
src/render/    OpenGL ES 2 renderer
src/platform/  SDL and OpenGL glue
  gl.c         loads GL functions at runtime
src/viewer.c   the level viewer
src/main.c     startup and main loop
src/third_party/ stb_image (public domain / MIT)
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
