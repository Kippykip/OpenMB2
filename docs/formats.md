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
runtime). `movex`/`movey` scroll the texture and `scalex`/`scaley`
appear on the water texture; their units aren't confirmed yet.

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
the picture is stretched to the screen. The intro flies in along one of 15
built-in spline paths (rand() % 15); OpenMB2 doesn't do that yet.

## Models, `.x` (confirmed)

DirectX `.x` files, `xof 0303txt 0032` (26 files, mostly `Meshes/`) and
`xof 0303bin 0032` (53 files, mostly `SkinMeshes/`). The loader
(`src/core/xfile.c`) reads both, applies the frame hierarchy and flattens
each model to triangles grouped by material. Skin weights and animation
sets are skipped for now. Frame names matter: boards have `Doska` (the
floor) and `Stenki` (the walls).

## Still to look at

- Animation sets in `SkinMeshes/` (bats, animals, prizes).
- What brick record bytes 10, 11, 17-19 do, and the non-brick objects.
- Font layout files in `Text/`.
- Ball physics, prize effects and scoring: these live in the original exe,
  and will be checked there one function at a time.
