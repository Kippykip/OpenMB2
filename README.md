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
- [x] Reads level files, and `params.txt` the first time
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

or copy `openmb2.exe` into the game folder and run it from there. Any
release works: the ones that keep their data packed (the `.pak` files,
or the disc's `MagicBall2.dat`) are read as they are, no unpacking
needed. It
starts like the original: the splash screen, the Alawar logo (any key
or click skips it), then the main menu, with
the level packs, level pages, options, how to play and high scores. The
settings, high scores (`score.dat`) and opened levels (`s<pack>.sav`)
are the original's own files, so both games share them. In a level:

| Key | Does |
| --- | --- |
| mouse drag, wheel | turn and zoom the camera |
| Space | reset the camera |
| F10 | smooth motion on / off |
| Alt+Enter | fullscreen |
| Escape | pause (which frees the mouse); again for the main menu |
| S / L | save / load the game (default.save, shared with the original) |
| Shift+1-3 / 1-3 | save / load the hidden quick slots |
| Shift+F11 | skip the level (a developers' cheat in the original) |

`--cheats` turns on the testing keys below. Online scores are still
shown but not sent while it is on.

| Key | Does |
| --- | --- |
| Left / Right | previous / next level |
| Up / Down | next / previous level pack |
| Numpad 1 / 2 | bigger / smaller bat |
| Numpad 3 | triple ball |
| Numpad 4 / 5 | faster / slower balls |
| Numpad 6 | yo-yo |
| Numpad 7 / 8 / 9 / 0 | magnet / gun / bazooka / cannon |
| Numpad . | hypnotic ball |
| Numpad / (Shift: back to normal) | powerball |
| Numpad * (Shift: back to simple) | explosive ball |
| Numpad + / - | extra life / bat killer |
| Numpad Enter | next-level medal |

A prize from the numpad drops in front of the bat and clears the wait
between prizes, so they can come one after another.

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

The bats with one fan and with none have no bounce for a ball hitting
near their ends, so in the original they stay still; OpenMB2 gives them
their bounce from the middle, and `--original-bounce` keeps them still.

The original moves the bat in steps of about 1% of the board for each
count from the mouse, and its sensitivity drops the fraction, so slow
moves are lost and the bat jumps. OpenMB2 moves it by exactly what the
mouse reports, and the sensitivity slider goes from 1/8 of the
original's speed (left) to all of it (right), so the middle is about a
third. `--original-mouse` brings back the original's movement.

On a screen that isn't 4:3 the menus keep their shape between black
bars; `--wide-menus` fills the screen instead, cutting off the
background's edges.

The online high scores work again. The original's server is long gone;
OpenMB2 speaks its protocol to the community's revival at
`https://kippykip.com/mb2s` (the same server mb2fix.dll's `ScoreServer`
points the original at), so both games share one table. A score of a
million or more is sent once its name is entered, as in the original,
and the high scores page's online table shows the pack's best, with
yours blinking.

### The OpenMB2 folder

A folder named `OpenMB2` in the game folder is read before the game's
own files: `OpenMB2/Menu/lmainmenu.txt` is used in place of
`Menu/lmainmenu.txt`, and a file the game doesn't have is added. The
game's files stay untouched, so the original still runs as it was, and
without the folder OpenMB2 plays the game as it is. Saves and scores
are always the game's own, shared with the original.

`data/OpenMB2` here holds what OpenMB2 adds; copy it into the game
folder, or point `--overrides` at the folder the OpenMB2 folder is in
(`--overrides data`) to use it from there instead. For now that is French and Russian (`Languages/French.txt`,
`Languages/Russian.txt`).
Languages only the OpenMB2 folder has come after the game's own on the
options page. Language files use the game's font, which keeps its
accented letters at 0x80 on (à is 0x80, é is 0x89, ü is 0x9c). A
language whose letters the font lacks brings its own:
`Textures/Font_<language>.png` and `Text/Text_<language>.txt` are used
while it is picked. Russian's font puts а to я at 0x80 to 0xa0.

### openmb2.ini

OpenMB2 keeps all its settings in `openmb2.ini` next to `params.txt`.
The first time it runs it takes what it can from the original's
`params.txt`, then writes `openmb2.ini` with comments; it reads
`params.txt` but never writes it, so the original keeps its own
settings. The options page has a page for each section but `[options]`
and `[online]`.

- `[gameplay]`: `language` (a file's name in Languages), `mouse_speed`
  (0.5 to 1), `xray_ball`, and `skip_transitions` to start levels
  without the camera's flight in.
- `[video]`: `width` and `height` set the window's size, or the screen
  mode in full screen; 0 x 0 is "Auto": the desktop's own size in full
  screen, else an 800 x 600 window, which opens maximised if it was
  last time (`maximized`). The options page offers Auto and every size
  your display has; resizing the window by hand doesn't change it.
  params.txt's video mode isn't used. `windowed`, `vsync`, and `interpolate` (smooth motion
  between game steps; F10 switches it for the run).
- `[audio]`: `sound_volume` and `music_volume`, 0 to 100.
- `[joystick]`: `enabled` (on by default), `vibration` and
  `sensitivity` (0 to 100). They are kept for joystick support to come.
- `[options]`: what OpenMB2 does differently, as `on`/`off`:
  `sort_glass`, `edge_bounce`, `precise_mouse`, `wide_menus`.
- `[online]`: `score_server` is the online scores' server (empty turns
  them off); `id` is the id the server knows you by where the
  original's (the C: drive's serial number) can't be had, as on Linux.

A switch on the command line wins over the file for that run. An
`openmb2.ini` from before keeps working: `[display]` is read as
`[video]`, and `fullscreen`, `smooth`, `level_intro` and `language`
are read where they were.

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
  molebox.c    the packed releases' .pak and .dat files
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
src/online.c   the online high scores (core/des3.c encrypts them)
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
