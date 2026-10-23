/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "level.h"
#include "../core/log.h"
#include "../core/vfs.h"

#include <stdlib.h>
#include <string.h>

#define LEVEL_MAGIC        "Magic Ball 2 Level"
#define LEVEL_SLOTS_OFFSET 0x15
#define LEVEL_DATA_OFFSET  (LEVEL_SLOTS_OFFSET + LEVEL_SLOT_COUNT * LEVEL_SLOT_SIZE) /* 0x215 */

bool level_parse(Level *level, const uint8_t *data, size_t size, const char *source_name)
{
    size_t record_bytes;
    int i;

    memset(level, 0, sizeof(*level));

    if (size < LEVEL_DATA_OFFSET || memcmp(data, LEVEL_MAGIC, strlen(LEVEL_MAGIC)) != 0) {
        log_warn("%s: not a Magic Ball 2 level", source_name);
        return false;
    }

    level->header_byte = data[0x14];

    for (i = 0; i < LEVEL_SLOT_COUNT; i++) {
        /* Slots are NUL padded but not guaranteed to be NUL terminated. */
        memcpy(level->slots[i], data + LEVEL_SLOTS_OFFSET + i * LEVEL_SLOT_SIZE, LEVEL_SLOT_SIZE);
        level->slots[i][LEVEL_SLOT_SIZE] = '\0';
    }

    record_bytes = size - LEVEL_DATA_OFFSET;
    if (record_bytes % LEVEL_RECORD_SIZE != 0)
        log_warn("%s: %u stray bytes after the last object", source_name,
                 (unsigned)(record_bytes % LEVEL_RECORD_SIZE));

    level->object_count = (int)(record_bytes / LEVEL_RECORD_SIZE);
    if (level->object_count == 0)
        return true;

    level->objects = calloc((size_t)level->object_count, sizeof(LevelObject));
    if (level->objects == NULL) {
        level->object_count = 0;
        return false;
    }

    for (i = 0; i < level->object_count; i++) {
        const uint8_t *record = data + LEVEL_DATA_OFFSET + (size_t)i * LEVEL_RECORD_SIZE;
        LevelObject *object = &level->objects[i];

        memcpy(object->raw, record, LEVEL_RECORD_SIZE);
        object->type = record[0];
        object->x = (int8_t)record[1];
        object->y = (int8_t)record[2];
        object->z = (int8_t)record[3];
    }
    return true;
}

bool level_load(Level *level, const char *game_path)
{
    size_t size;
    uint8_t *data = vfs_read(game_path, &size);
    bool ok;

    if (data == NULL) {
        memset(level, 0, sizeof(*level));
        log_warn("%s: file not found", game_path);
        return false;
    }
    ok = level_parse(level, data, size, game_path);
    free(data);
    return ok;
}

void level_free(Level *level)
{
    free(level->objects);
    memset(level, 0, sizeof(*level));
}

bool level_object_is_brick(const LevelObject *object)
{
    return object->type <= 0x38;
}
