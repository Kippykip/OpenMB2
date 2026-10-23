/*
 * level.h - loader for .mb2 level files.
 *
 * Layout (all levels seen so far, every release):
 *
 *   0x000  20 bytes   "Magic Ball 2 Level", NUL padded
 *   0x014  1 byte     unknown, always 0x22 so far (version?)
 *   0x015  8 x 64     file name slots, NUL padded. In use:
 *                       0 texture set   ("textureset.txt")
 *                       1 board         ("Boards/boardround1.txt")
 *                       2 background    ("Backgrounds/bgnone.txt")
 *                       3 pad / bat     ("Pads/common.txt")
 *                     slots 4-7 have always been empty.
 *   0x215  N x 20     object records, until the end of the file
 *
 * Object record (20 bytes):
 *   0      type     0x00-0x38 bricks; other codes match the hidden
 *                   editor's tools (0x84 teleport in, 0x88 teleport out,
 *                   0x90 fire, 0x94 water, 0xa0 cannon, 0xb8 sheep...)
 *   1      x        signed grid position (no two objects share x/y/z)
 *   2      y        signed grid position (height / layer)
 *   3      z        signed grid position (row)
 *   4-7    colour   always ff ff ff ff so far; byte order not confirmed
 *   8-19   unknown  kept in raw[] until they are decoded
 */
#ifndef OPENMB2_LEVEL_H
#define OPENMB2_LEVEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LEVEL_SLOT_COUNT  8
#define LEVEL_SLOT_SIZE   64
#define LEVEL_RECORD_SIZE 20

enum {
    LEVEL_SLOT_TEXTURESET = 0,
    LEVEL_SLOT_BOARD = 1,
    LEVEL_SLOT_BACKGROUND = 2,
    LEVEL_SLOT_PAD = 3
};

typedef struct LevelObject {
    uint8_t type;
    int8_t x, y, z;
    uint8_t raw[LEVEL_RECORD_SIZE]; /* the whole record, as stored */
} LevelObject;

typedef struct Level {
    uint8_t header_byte; /* the unknown byte at 0x14 */
    char slots[LEVEL_SLOT_COUNT][LEVEL_SLOT_SIZE + 1];
    LevelObject *objects;
    int object_count;
} Level;

/* Parse a level from memory. Returns false if it isn't a level file. */
bool level_parse(Level *level, const uint8_t *data, size_t size, const char *source_name);

/* Load a level from the game folder. */
bool level_load(Level *level, const char *game_path);

void level_free(Level *level);

/* True for record types that are ordinary bricks. */
bool level_object_is_brick(const LevelObject *object);

#endif
