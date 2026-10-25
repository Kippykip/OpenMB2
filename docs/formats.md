# Magic Ball 2 data formats

What we know about the files OpenMB2 reads. "Confirmed" means checked
against every file in the Community Edition folder (Big Fish 2005 engine,
all three level packs); anything else is marked as a guess.

## Game folder layout

| Folder | Contents |
| --- | --- |
| `Backgrounds/` | background definitions (config), e.g. `skinmesh: fon.x` |
| `Boards/` | board definitions (config): size, camera, board mesh |
| `Bullets/` | bat weapon projectiles (config) |
| `Languages/` | UI text, one line per string (see below) |
| `Levels/`, `Levels1/`..`Levels7/`, `Levels0r/` | level packs: `.mb2` levels plus `Menu/` level-select screens |
| `Menu/` | menu screens and widgets (config) |
| `Meshes/` | static models, DirectX `.x` (text) |
| `Music/` | music cues (config) pointing into `mb01.mo3` |
| `Pads/` | bat definitions (config) |
| `Prizes/` | power-up definitions (config) |
| `SkinMeshes/` | animated models, DirectX `.x` (binary, with `AnimationSet`) |
| `Sounds/` | Ogg Vorbis sound effects |
| `Text/` | font layout for the font textures |
| `Textures/` | `.jpg`/`.png`/`.dds` images; `Textures/Scripts/` holds texture definitions |

Pack numbers (used by saves and online scores): 0 = `Levels`,
1-7 = `Levels1`-`Levels7`, 8 = `Levels0r`.

File names are not case-consistent, and both `/` and `\` appear. OpenMB2
looks everything up case-insensitively (`src/core/vfs.c`).

## MoleBox boxes, `.pak` and `.dat` (confirmed)

Every release before Smash Frenzy 2 keeps these folders in MoleBox
boxes, which the exe's built-in MoleBox runtime shows the game as files
on disk: `Default.pak`, `Interface0.pak`, `Lev0.pak` (and `Lev1.pak`,
`Lev0r.pak` in New Worlds) in the 2005 builds, `MagicBall2.dat` on the
2004 disc. In OpenMB2 loose files win over a box's. `src/core/molebox.c` reads
them; the keys were taken from the runtimes by running them in an
emulator.

- **Trailer**, the last 44 bytes: six u32s (directory packed size,
  directory size, directory offset, name table size, file count, box
  size), 16 bytes not needed, and a u32 equal to `"MBOX"` ^ `0xcafebabe`
  ^ the box's size. Offsets count from the box's start, which is the
  file size minus the box size.
- **Directory**: IDEA in ECB mode, the last partial block left plain.
  The key is fixed per runtime version: `c14c1d8c37ab94233dfa681280a34597`
  in every 2005 pak (RealArcade, New Worlds, Gold, the Polish release,
  Big Fish), `e93aca69e22a76c0872fa02ef67365f0` on the disc. It holds
  the names (NUL-terminated, backslashes, code page 1251), then 16 bytes
  per file: name offset, data offset, size, flags.
- **Files**: one u32 header per 64 KB chunk just before the data offset;
  bit 31 set means the chunk is stored, and every chunk in every box is.
  Flags `0x13` files are XORed with one byte per box (0x21 in the paks,
  0x88 on the disc, which OpenMB2 guesses from the files' first bytes);
  flags `0x33` (`Lev0r.pak`) are IDEA-encrypted with the same key.

## Config files (confirmed)

Almost every `.txt` outside `Languages/` uses this layout:

```
CMenuItemButton        optional class name on its own line
{
  width: 96            key: value
  menuitem: Menu/a.txt keys can repeat; order matters
  bricktexture         a bare word is a flag
}
```

- The value is everything after the first `:` (so `chr: charcode: 65 x1: 0`
  has key `chr` and value `charcode: 65 x1: 0`).
- Numbers can be written `12.` or `.25`. Colours are hex ARGB (`FFFFFF44`).
- Misspelt keys are ignored by the game, and the developers used that to
  switch things off (`meenuitem:` in `Menu/LOPTIONS.TXT`).
- Class names seen: `List`, `texture`, `CMenuItemButton`,
  `CMenuItemPanel`, `CMenuItemSelect`, `CMenuItemLevelSelect`,
  `CMenuHelpView`, and no class at all (boards, pads, prizes, params).

Parser: `src/core/config.c`. Check a folder with `mb2inspect <folder> scan`.

### Values that look like paths but aren't always

- `script:` depends on the button's `id:`. `navigate` → a menu file,
  `openurl` → a web address, `startgame` → a level pack folder.
- `updatetexture:` names a texture the game draws at runtime (the
  level-select thumbnails `Levels/levels1.jpg` and so on are not shipped).
- Model and texture names (`skinmesh: boardcom.x`, `texture: panel.png`) are
  bare file names; the folder depends on the kind of file. To be pinned down
  in milestone 2.

## params.txt (confirmed)

Config without a class. Keys: `soundvolume`, `musicvolume` (0-100),
`window` (1 = windowed), `language` (index into `Languages/`, alphabetical),
`resolution` (index into the original mode list), `hallfame`,
`infravision`, `act` (online activation; unused), `joystick`, `msensivity`.
Loaded by `src/game/params.c`.

## Levels, `.mb2` (layout confirmed, brick fields read from the exe)

| Offset | Size | Contents |
| --- | --- | --- |
| 0x000 | 20 | `Magic Ball 2 Level`, NUL padded |
| 0x014 | 1 | unknown, always `0x22` |
| 0x015 | 8 × 64 | file name slots, NUL padded: 0 texture set, 1 board, 2 background, 3 pad; 4-7 always empty |
| 0x215 | N × 20 | object records to the end of the file |

All 301 levels have a size of `0x215 + N × 20`.

Object record:

| Byte | Meaning |
| --- | --- |
| 0 | type. `0x00`-`0x38` bricks; others match the hidden editor's tool codes (`0x84`/`0x88` teleport in/out, `0x90` fire, `0x94` water, `0xa0` cannon, `0xa4` fireworks, `0xb8` sheep, `0xbc` elk, `0xc0` ostrich, `0xc8` camel, `0xca` giraffe, `0xce` rabbit, `0xdc` elephant, `0xde` crocodile...) |
| 1, 2, 3 | signed grid position x, y, z: x across, y up, z away from the bat. The brick is placed at (x − 0.01, y, z). No two objects in a level share a position. |
| 4-7 | colour, always `ff ff ff ff` so far |
| 8 | flags. `0x80` adds an overlay model `Meshes/<model>dophgt<height>.x` drawn with texture index byte 14 + 1 and tile byte 16; `0x20` / `0x40` mirror the overlay's U / V. Always 0 in the levels checked so far. |
| 9 | rotation bits, applied about X, then Y, then Z, then moved into place: `0x01` X 90°, `0x02` X 180°, `0x04` Y 22.5°, `0x08` Y 45°, `0x10` Y 90°, `0x20` Y 180°, `0x40` Z 90°, `0x80` Z 180° |
| 10, 11 | unknown |
| 12 | model number: `Meshes/<n>.x` |
| 13 | height in layers. 1 uses `Meshes/<n>.x`, h > 1 uses `Meshes/<n>height<h>.x` (e.g. `0HEIGHT2.X`) |
| 14 | texture script index, in `registertexture` order in the level's texture set (stock: 0 t_fake, 1 t_lighting, 2 t_water, 3 t_interface, 4-9 t_tileset1-6, 10 t_billboard, ...) |
| 15 | tile in the texture sheet: column in the high nibble, row in the low nibble. UVs become `(u + column) × xfactor`, `(v + row) × yfactor`. |
| 16 | overlay tile (see byte 8) |
| 17-19 | unknown |

The original bakes every brick into world space when the level loads
(rotate, translate, remap UVs) and draws all bricks of one texture together.

Loader: `src/game/level.c`. Dump one with `mb2inspect <folder> level <file>`.

## Language files (confirmed, from the earlier reverse engineering)

Plain lines, not config. A `/// NAME ///` header jumps the string counter to
a fixed slot: SYSTEM 0, ENDGAME 32, OPTIONS 48, INFO 96, TRIAL 112,
PRIZES 128, PACKS 160, NAG-SCREEN 192, RUNNINGSTRING 208, CREDITS 224.
Menu widgets pick a string with `textindex:`. The text uses the game font's
own code page, not Latin-1 (see the project findings for the mapping).

## Music (confirmed)

All music is one tracker module, `Music/mb01.mo3`. Each cue file
(`MB0`-`MB3`, `MENU`, `ENTER`, `INTRO`) gives a start `order:` and `row:`
in it. OpenMB2 will play it with libopenmpt, which reads MO3.

## Texture sets and scripts (confirmed, flag meanings partly guessed)

`textureset.txt` holds the scene light (`lightdir: 0 -4 1`, and
`ambientcolor`, `diffusecolor`, `specularcolor` as hex ARGB) and a list of
`registertexture:` script paths. A script looks like:

```
texture
{
  texture: tileset01.jpg     image in Textures/
  xfactor: 0.125             tile width in UV units (8 tiles across)
  yfactor: 0.125
  bricktexture               flags, see below
  deletedead
}
```

Flags seen: `alpha` (blended), `zdisable` (no depth writes, a guess),
`twosided`, `notlighted`, `billboard`, `interface`, `bricktexture`,
`deletedead`, `specular`, `faketexture` (no image), `updated` (drawn at
runtime). Each frame the texture is drawn (0x410110) `movex`/`movey`
are added to its UV offset, and `scalex`/`scaley` to its UV scale, which
turns round at 0.75 and 1.25 (starting at 0.75). Water (`scaley: 0.0025`,
`movex: 0.005`) therefore drifts sideways while its ripples stretch one
way and back, turning round every 200 frames (about 3.3 s at 60 fps).

The original registers every file in `Textures/` (not subfolders) at
startup. To load an image it strips the extension and tries `.png`,
`.dds`, `.jpg`, `.bmp` in `Textures/`. Direct3D stretches every image to
power-of-two sides, and OpenMB2 does the same. `tileset04.dds` is DXT5.

## Boards and backgrounds (confirmed)

Board files: shape flag `rect`, `round` or `compaund`; `halfwidth`,
`halfheight`, `radius`, `batline`; `scalex` multiplies halfwidth and
radius, `scalez` multiplies halfheight and batline; `camera: <look-at z>
<distance>`; `skinmesh:` a model in `SkinMeshes/`.

The board model is drawn stretched by (scalex, 1, scalez). All three stock
board models put the top of the `Doska` floor at y = −0.314, but bricks
stand on y = −1, and the original shows the board that much lower, so the
model is also moved down until the floor top is at −1 (checked against
side-by-side screenshots of the first three levels).

Background files only have `skinmesh:` (`fon.x`, a large box around the
board). There is no sky: `fon.x` names `01.jpg`, which isn't shipped.
A level with an empty background slot (e.g. `030_Volkano`) still gets the
usual box, so OpenMB2 falls back to `bgnone.txt`.

`randomizetexture` is not a file key but a command the game sends when a
level starts, and again on F8 (board) and F7 (background). For the board
it gives the `Doska` frame a random `Board/Square|Round|SemicircularNN.jpg`
(NN = rand() % 10, the shape from the board's shape flag); for the
background a random `BG/bgNN.jpg` (NN = rand() % 28).

## Drawing details (confirmed from screenshots)

- Bricks are drawn without back-face culling. One-sided grass and leaf
  models look dark otherwise, because their lit side faces away.
- No mipmaps: Direct3D 8 doesn't make them for a plain texture load, and
  with them the floor and water look blurred compared with the original.
- Object type 0x18 is an invisible brick (texture 7, tile 0xF0), not drawn
  (probably until hit). Type 0x10 bricks cycle colour (the level 3 stars); OpenMB2
  doesn't animate them yet.

## Camera (confirmed)

The camera orbits the point (0, 0, look-at z) from the board file. At the
start of a level: yaw 0, pitch 50° (0.8727 rad), roll 0.05, zoom 1.
Eye = (0, 0, −distance × zoom) turned by `YawPitchRoll(yaw, pitch, roll)`,
view = `LookAtLH(eye, (0, 0, look-at z), up (0, 1, 0))`. Projection is
`PerspectiveFovLH(45°, aspect 1.0, near 1, far 1000)` in 4:3 modes, so
the picture is stretched to the screen. Dragging with the right
button turns it by 0.005 per mouse step, yaw clamped to ±60° and pitch to
15°–90°; the wheel adds lZ × 0.00001 to the zoom, clamped to 0.25–1.75
(camera 0x48f088: 0x409ed0, 0x409e80, 0x409f20).

### Level intro and fade-out

At the start of a level (0x409f70) the camera flies in for 4 s: the eye
follows `D3DXVec3Hermite(p1, t1, p2, t2, s)`, s += frame ms × 0.00025,
looking at (0, 0, look-at z) with up (0, 1, 0), and p2 is the usual eye.
One of 15 paths is picked with rand() % 15; with d the board's camera
distance (p1; t1; t2):

| # | p1 | t1 | t2 |
|---|---|---|---|
| 0 | (d, 0, d) | (0, d, 0) | (−2d, −d, 0) |
| 1 | (−d, 0, d) | (0, d, 0) | (2d, −d, 0) |
| 2 | (d, 0, 0) | (d, d, d) | (−d, d, −d) |
| 3 | (−d, 0, 0) | (−d, d, d) | (d, d, −d) |
| 4 | (d, 0, −d) | (d, d, d) | (−d, −d, 0) |
| 5 | (−d, 0, −d) | (−d, d, d) | (d, −d, 0) |
| 6 | (−d, d, −d) | (d, d, d) | (d, −d, 0) |
| 7 | (d, d, −d) | (−d, d, d) | (−d, −d, 0) |
| 8 | (0, d, 0) | (0, 0, d) | (0, −d, 0) |
| 9 | (d, d, 0) | (0, −d, d) | (−d, d, 0) |
| 10 | (−d, d, 0) | (0, −d, d) | (d, d, 0) |
| 11 | (−1.75d, d, 0) | (0, d, d) | (d, 2d, 0) |
| 12 | (1.75d, d, 0) | (0, d, d) | (−d, 2d, 0) |
| 13 | (−d, d, 0) | (2d, d, −2d) | (−2d, −d, 2d) |
| 14 | (d, d, 0) | (−2d, d, −2d) | (2d, −d, 2d) |

While it flies, dragging and the wheel do nothing and a click can't serve
or fire (0x4055e0); the bat still moves.

When the last brick goes (state 0x4000) or the last life (0x800 / 0x8000)
the game stops updating and, over 1 s of frame time (0x41b515), the
light's diffuse and ambient colours are blended towards 0.05 in every
channel; unlit things such as the HUD keep their brightness. Then the
next level loads (or the game goes on to the menus).

## Models, `.x` (confirmed)

DirectX `.x` files, `xof 0303txt 0032` (26 files, mostly `Meshes/`) and
`xof 0303bin 0032` (53 files, mostly `SkinMeshes/`). The loader
(`src/core/xfile.c`) reads both, applies the frame hierarchy and flattens
each model to triangles grouped by material. Skin weights and animation
sets are skipped for now. Frame names matter: boards have `Doska` (the
floor) and `Stenki` (the walls).

## Fonts and the HUD (confirmed)

`Text/text.txt` lays out `Textures/font.png`, and `Text/service.txt` lays
out `service.png` (the corner bars C-F) and `icons.png` (weapon icons G-K).
A font starts as a grid of 20x32 cells on a 256x256 sheet, 12 to a row:
A-Z, 0-9, then the codes from 0x80 up (0x426d30). The file then changes
single glyphs, in file order (0x4279f0):

| Line | Effect |
|---|---|
| `chr: charcode: N x1: y1: x2: y2:` | glyph anywhere on the sheet; x2 < x1 mirrors it (E and F are C and D flipped) |
| `charwidth: X n` | cell n+1 pixels wide; A-Z also set a-z |
| `charheight: X n` | cell n+1 pixels high |
| `chardopwidth/chardopheight: X n` | drawn size only |

Text is placed in an 800x600 screen centred on 0, y up (0x4271f0). Bytes
0xC0-0xD9 in a string resize or nudge the rest of the line and `|` starts
a new line. Strings come from `Languages/<name>.txt` by line number, from
0 for the first line, except that a section line moves the numbering on
(0x418950): ENDGAME 0x20, OPTIONS 0x30, INFO 0x60, TRIAL 0x70, PRIZES
0x80, PACKS 0xA0, NAG-SCREEN 0xC0, RUNNINGSTRING 0xD0, CREDITS 0xE0; params.txt `language` picks the file by its place in the
folder.

The HUD (0x41b818, 0x404110), all white:

| What | Font | At | Scale | Align |
|---|---|---|---|---|
| bar C | service | (-401, 278) | 0.85 | left |
| bar D | service | (-401, -278) | 0.85 | left |
| bar F | service | (401, -278) | 0.85 | right |
| bar E | service | (401, 278) | 0.85 | right |
| weapon icon J/H/G/K/I | icons | (330/320/320/310/310, 284) | 0.95 | right |
| shots, or yo-yo seconds | text | (350 gun, else 340, 286) | 0.95 | centre |
| "Score %d" (string 13) | text | (-375, 286) | 0.85 | left |
| "Lives %d" (14, never below 0) | text | (-375, -282) | 0.9 | left |
| "Level %d" (15) | text | (375, -282) | 0.9 | right |

Messages at the bottom (0x4262a0, 0x4263a0): text font, scale 0.65,
centred at (0, -280); each new one pushes the others up 24, and a sixth
is dropped. Each lasts 5 s, fading in over the first second (alpha =
age / 4) and out over the last. They come from prizes (string 0x80 +
prize kind, in the prize file's `messagecolor`), "Yo-Yo Time Over" and
"Hypnotic Ball Time Over" (21 and 20, red; the second only if a ball was
still hypnotic), and at level start strings 24-25 (0xFFFF4444) when the
level has fire (type 0x64 or 0x90) and 26-27 (0xFF4444FF) for a fountain
(0x68 or 0x94). They keep fading while the game is paused.

Pause (0x41bee4): Escape while playing pauses, Escape while paused goes
to the main menu, and any other key (or a click) goes on. Losing focus
pauses too. The overlay is strings 16-19 at (0, 60) scale 1.5 in red,
then (0, 0), (0, -40), (0, -80) in 0xFFFF4400, each over a black copy at
(+2, -2).

The original stretches this over any screen. OpenMB2 sizes it by the
screen's height and keeps each piece its distance from its own side, so
4:3 is unchanged and wider screens move the corners outwards.

## Saved games (confirmed)

S saves to `default.save` and L loads it; Shift+1-3 save to
`slot1-3.save` and 1-3 load (0x41c08a), all only during a level. A save
keeps the score and lives the level started with, so loading plays that
level again from the start (0x415040, 0x415da0). The layout is in
`src/game/save.h`; files written by either game load in the other.
Shift+F11 sets the bricks left to 0, finishing the level.

## Still to look at

- Animation sets in `SkinMeshes/` (bats, animals, prizes).
- What brick record bytes 10, 11, 17-19 do, and the non-brick objects.
- Ball physics, prize effects and scoring: these live in the original exe,
  and will be checked there one function at a time.
