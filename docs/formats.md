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

## Levels, `.mb2` (layout confirmed, record fields partly guessed)

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
| 1, 2, 3 | signed grid position x, y, z (guess: x across, y height, z rows). No two objects in a level share a position. |
| 4-7 | colour, always `ff ff ff ff` so far |
| 8-19 | unknown. Byte 12 varies by brick (0x06, 0x08, 0x20, 0x28...), byte 15 looks like a tile index (0x23, 0x33, 0x44 = column/row in an 8×8 tile sheet?). To be decoded in milestone 2. |

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

## Still to look at

- DirectX `.x` meshes, text and binary, with skinning and animation.
- Texture definition flags (`bricktexture`, `deletedead`, `billboard`,
  `zdisable`, `xfactor`/`yfactor` tile sizes, `movex`/`scaley` scrolling).
- Font layout files in `Text/`.
- Ball physics, prize effects and scoring: these live in the original exe,
  and will be checked there one function at a time.
