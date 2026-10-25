# OpenMB2

![OpenMB2's Revamped Options Menu](https://cdn.kippykip.com/images/openmb2/openmb2-options.png)

An open reimplementation of the Magic Ball 2 / Magic Ball 2: New Worlds
engine, written from scratch in plain C so it can run anywhere SDL2 and
OpenGL ES 2 can run, including Windows, Linux, ARM handhelds, Android/iOS and web
browsers.

OpenMB2 contains **no game data**. It loads the files from your own copy of
the game, so you need a Magic Ball 2 or Smash Frenzy 2 install.

## Status / Roadmap

Version 1.0: the whole game plays from start to finish.

- [x] Reads the game folder, loose or packed (`.pak` / `.dat`), with
      Windows-style file names on any system
- [x] Every level in 3D with its board, background, camera and intro
- [x] The bat, balls, bricks, prizes, weapons, cannons, teleports,
      water, fire and animals, with the original's rules
- [x] Sound and tracker music
- [x] Menus, fonts, languages, options, saves and high scores, shared
      with the original
- [x] Online high scores
- [x] Keyboard controls, joypads and rumble
- [ ] Web, Android and handheld builds

## Engine Enhancements

- [x] Full frame interpolation, supporting framerates way beyond 60FPS
- [x] Completely overhauled how the game handles resolutions and windowed mode
(VSync, Widescreen, Resizable Windows, HUD with correct aspect ratio, etc)
- [x] Mod support (OpenMB2 Directory)
- [x] Multilingual (English, French, German, Polish, Russian)
- [x] Keyboard navigation and keyboard controls
- [x] Full controller support and rumble
- [x] Overhauled options menu with many new options and sub-menus
(Gameplay Options, Video Options, Audio Options, Joystick Options)
- [x] Cheats!
- [x] Improved handling of transparent blocks
- [x] Plus many other smaller fixes and improvements!


## Instructions


Copy both the `openmb2.exe` executable and the `OpenMB2` directory into the original game's folder and run `openmb2.exe` from there.  
Alternatively you can use two command line arguments `--game` to point to the original game's assets and `--overrides` to point the engine to the folder that contains the OpenMB2 directory.
Any release of Magic Ball 2 / Smash Frenzy 2 works, including the ones that keep their data in packed `.pak` files.
It starts like the original: the splash screen, the 3D Alawar logo (if all assets are present), then the main menu, with the level packs your version contains, level pages, options, how to play and high scores etc. High scores (`score.dat`) and opened levels (`s<pack>.sav`) are fully cross-compatible with the original game's engine.

**Menu Controls:**

| Mouse / Keyboard | Controller Button | Does|
| --- | --- | --- |
| Mouse, Up / Down / Left / Right Arrow keys | Left Stick, D-Pad| Navigates the menu |
| Left Mouse, Enter | A (Xbox), Cross (Sony), A (Nintendo) | Selects the menu item |
| Escape | Start, B (Xbox), Circle (Sony), B (Nintendo) | Goes back to the main menu |

**In-game Controls:**

| Mouse / Keyboard | Controller Button | Does|
| --- | --- | --- |
| Mouse, Left / Right Arrow keys | Left Stick, D-Pad| Moves the bat |
| Left Mouse, Space | A (Xbox), Cross (Sony), A (Nintendo) | Fires weapons / launches the ball |
| Right Mouse drag / Mouse Wheel | Right Stick | Turns / zooms the camera |
| F6 | N/A | Changes the music track |
| F7 | N/A | Changes the background |
| F8 | N/A | Changes the board texture |
| F9, Middle Mouse Button | Left / Right Stick Button | Resets the camera |
| F10 | N/A | Toggles frame interpolation on / off |
| Alt+Enter | N/A | Toggles fullscreen |
| Escape | Start | Pauses; again for the main menu (any other key or button resumes) |
| S / L | L1 / R1 (while paused) | Saves or loads the savegame |
| Shift+1-3 / 1-3 | N/A | Saves or loads three additional quick slots |
| Shift+F11 | N/A | Skips the level (a developers' cheat in the original) |

`--cheats` turns on the testing keys below (and Left / Right then
change level instead of moving the bat). Online scores are still
shown but not sent while this setting is on.

| Key | Does |
| --- | --- |
| Left / Right | Previous / next level |
| Up / Down | Next / previous level pack |
| Numpad 1 / 2 | Bigger / smaller bat |
| Numpad 3 | Triple ball |
| Numpad 4 / 5 | Faster / slower balls |
| Numpad 6 | Yo-Yo |
| Numpad 7 / 8 / 9 / 0 | Magnet / Gun / Bazooka / Cannon |
| Numpad . | Hypnotic ball |
| Numpad / (Shift: back to normal) | Powerball |
| Numpad * (Shift: back to simple) | Explosive ball |
| Numpad + / - | Extra life / bat killer |
| Numpad Enter | Next-level medal |

**Other Command Line Arguments:**

- `--level "Levels1/001_Vesi.mb2"` starts on a level instead of the menus
- `--menu Menu/loptions.txt` starts on a menu page
- `--no-intro` skips each level's opening camera flight
- `--screenshot shot.bmp` saves the first frame and quits

The game logic itself runs in fixed 18 ms steps, like the original does. On
screens faster than that, OpenMB2 draws the balls, prizes and shots part
way between their last two steps so they move smoothly, and falling,
tipping and shrinking bricks too; the rules are
untouched. `--no-smooth` (or F10) draws them only where each step leaves
them, like the original.

The original draws bricks texture by texture, so from some angles glass
shows over glass in front of it, or a solid brick behind shows through.
OpenMB2 draws solid bricks first and glass from the back forwards;
`--original-glass` goes back to the original's order.

The two extra bat models (one fan and no fans) don't have a bounce animation for a ball hitting their edges, so in the original they don't animate at all; OpenMB2 makes these two bats play the center bounce animation instead, and `--original-bounce` reverts to the original engine's behaviour.

The original mouse controls move the bat in steps of about 1% of the board for each
count from the mouse, and its sensitivity drops the fraction, so slow
moves are lost and the bat jumps. OpenMB2 moves it by exactly what the
mouse reports, and the sensitivity slider goes from 1/8 of the
original's speed (left) to all of it (right), so the middle is about a
third. `--original-mouse` brings back the original's movement.

On a screen that isn't 4:3 the menus keep their shape between black
bars; `--wide-menus` fills the screen instead, cutting off the
background's edges.

The online high scores work again. The original's server is long gone;
OpenMB2 uses a new high score server hosted at `kippykip.com` (the same server previously used by mb2fix.dll). A score of a
million or more is sent once its name is entered, as in the original,
and the high scores page's online table shows the pack's best, with
yours blinking.

### The OpenMB2 folder

A folder named `OpenMB2` in the game's folder is read with priority over the game's
own original files, so modifications can be added in here without overwriting the original game's assets.  

`data/OpenMB2` contains the folder found in the releases; either copy it into the game
folder, or point to it via the `--overrides` command line argument.
(e.g. `--overrides data`). It holds the additional options menu pages, the extra languages, and the credits page's OpenMB2-exclusive rows.
Languages whose letters need their own font bring it with them:
`Textures/Font_<language>.png` and `Text/Text_<language>.txt` are used
while that language is picked. Russian is an example of one.

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
- `[joystick]`: `enabled` (on by default), `vibration`, and
  `sensitivity` (0 to 100, 50 crosses the board in about 0.8 s; also
  the arrow keys' speed).
- `[options]`: what OpenMB2 does differently, as `on`/`off`:
  `sort_glass`, `edge_bounce`, `precise_mouse`, `wide_menus`.
- `[online]`: `score_server` is the online scores' server (empty turns
  them off); `id` is the id the server knows you by where the
  original's (the C: drive's serial number) can't be had, as on Linux.

A switch on the command line wins over the file for that run. An
`openmb2.ini` from before keeps working: `[display]` is read as
`[video]`, and `fullscreen`, `smooth`, `level_intro` and `language`
are read where they were.

Saves and scores are separate files and are cross-compatible between both engines.

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
  a later version).

## Source layout

```
src/core/      no SDL or GL here; shared with the tools
  vfs.c        case-insensitive access to the game folder
  molebox.c    the packed releases' .pak and .dat files
  config.c     parser for the game's { key: value } files
  xfile.c      DirectX .x model loader
  image.c      texture decoding (jpg/png/bmp, DDS)
  matrix.c     Direct3D-style vectors and matrices
  des3.c       the online scores' encryption
src/game/      game data and rules, no SDL or GL either
  play.c       the rules: bat, balls, bricks, prizes, weapons
  level.c      .mb2 level loader
  board.c      board and background files
  textures.c   textureset.txt and texture scripts
  scene.c      builds a level's geometry (bricks baked like the original)
  font.c       the bitmap fonts
  lang.c       language files
  menu.c       menu pages
  save.c       saved games, in the original's format
  scores.c     high scores
  settings.c   openmb2.ini, and params.txt the first time
src/render/    OpenGL ES 2 renderer
src/audio/     sound and music (SDL, stb_vorbis, libopenmpt)
src/platform/  OpenGL loaded at runtime
src/play_screen.c  a level on screen: camera, HUD, pause, input
src/menu_screen.c  the menus
src/intro.c    the Alawar logo
src/pad.c      joypads and rumble
src/online.c   the online high scores
src/main.c     startup and main loop
src/third_party/ stb_image, stb_vorbis
res/           the Windows icon and version details
tools/         mb2inspect
docs/          file format and gameplay notes
```

The code aims to be easy to read: one job per file, plain C99, comments
that say *why*. See `docs/formats.md` for what's known about the data files.

## Credits

The OpenMB2 project was started by [Kippykip](https://kippykip.com), with extensive reverse engineering and programming help from Claude by Anthropic.

Magic Ball 2 was made by DreamDale and published by Alawar
Entertainment: code by Igor Perepechenko, graphics by Alexey "SCIA"
Chernov, sounds by Aleksander Shukaev, music by The Sands and the intro
by Yuri Rudakov. OpenMB2 is not made or endorsed by them.

OpenMB2 uses SDL2 (zlib licence), libopenmpt (BSD licence), and
stb_image and stb_vorbis (public domain).

## Licence

OpenMB2 is free software under the GNU General Public License, version 2 or
(at your option) any later version. See `LICENSE`.

This licence covers the OpenMB2 code only. Magic Ball 2's own files (levels,
models, textures, sounds, music) belong to their original owners and are not
part of this project.
