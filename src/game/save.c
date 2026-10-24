/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "save.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 0x138
#define LIST_SIZE (4 + SAVE_NAME_SIZE + SAVE_MAX_LEVELS * (SAVE_NAME_SIZE + 12))
#define PROGRESS_SIZE (SAVE_MAX_LEVELS * 12)

static const char k_magic[26] = "Magic Ball 2 saved game \n";

static void put_int(unsigned char *p, int value)
{
    uint32_t u = (uint32_t)value;
    p[0] = (unsigned char)u;
    p[1] = (unsigned char)(u >> 8);
    p[2] = (unsigned char)(u >> 16);
    p[3] = (unsigned char)(u >> 24);
}

static int get_int(const unsigned char *p)
{
    return (int)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void put_name(unsigned char *p, const char *name)
{
    size_t n = strlen(name);
    if (n >= SAVE_NAME_SIZE)
        n = SAVE_NAME_SIZE - 1;
    memcpy(p, name, n);
}

static void get_name(char *out, const unsigned char *p)
{
    memcpy(out, p, SAVE_NAME_SIZE);
    out[SAVE_NAME_SIZE - 1] = '\0';
}

/* Text mode: 0A goes to disk as 0D 0A. */
static unsigned char *to_text_mode(const unsigned char *raw, size_t raw_size, size_t *size)
{
    unsigned char *out = malloc(raw_size * 2);
    size_t i, n = 0;

    if (out == NULL)
        return NULL;
    for (i = 0; i < raw_size; i++) {
        if (raw[i] == '\n')
            out[n++] = '\r';
        out[n++] = raw[i];
    }
    *size = n;
    return out;
}

/* And back: 0D 0A reads as 0A. */
static size_t from_text_mode(const unsigned char *data, size_t size, unsigned char *out, size_t max)
{
    size_t i, n = 0;

    for (i = 0; i < size && n < max; i++) {
        if (data[i] == '\r' && i + 1 < size && data[i + 1] == '\n')
            continue;
        out[n++] = data[i];
    }
    return n;
}

unsigned char *save_encode(const SaveGame *save, size_t *size)
{
    unsigned char *raw = calloc(1, HEADER_SIZE + LIST_SIZE), *out, *p;
    int i;

    if (raw == NULL)
        return NULL;
    memcpy(raw, k_magic, sizeof(k_magic));
    put_int(raw + 0x20, save->score);
    put_int(raw + 0x24, save->lives);
    put_int(raw + 0x28, save->level);
    put_name(raw + 0x34, save->folder);
    p = raw + HEADER_SIZE;
    put_int(p, save->level_count);
    put_name(p + 4, save->progress_file);
    p += 4 + SAVE_NAME_SIZE;
    for (i = 0; i < SAVE_MAX_LEVELS; i++, p += SAVE_NAME_SIZE + 12) {
        put_name(p, save->names[i]);
        put_int(p + SAVE_NAME_SIZE, save->progress[i][0]);
        put_int(p + SAVE_NAME_SIZE + 4, save->progress[i][1]);
        put_int(p + SAVE_NAME_SIZE + 8, save->progress[i][2]);
    }
    out = to_text_mode(raw, HEADER_SIZE + LIST_SIZE, size);
    free(raw);
    return out;
}

bool save_decode(const unsigned char *data, size_t size, SaveGame *save)
{
    unsigned char *raw = malloc(HEADER_SIZE + LIST_SIZE);
    const unsigned char *p;
    size_t n;
    int i;

    if (raw == NULL)
        return false;
    n = from_text_mode(data, size, raw, HEADER_SIZE + LIST_SIZE);
    /* The original checks only the first 26 bytes (0x415de6). */
    if (n < HEADER_SIZE + LIST_SIZE || memcmp(raw, k_magic, sizeof(k_magic)) != 0) {
        free(raw);
        return false;
    }
    memset(save, 0, sizeof(*save));
    save->score = get_int(raw + 0x20);
    save->lives = get_int(raw + 0x24);
    save->level = get_int(raw + 0x28);
    get_name(save->folder, raw + 0x34);
    p = raw + HEADER_SIZE;
    save->level_count = get_int(p);
    get_name(save->progress_file, p + 4);
    p += 4 + SAVE_NAME_SIZE;
    for (i = 0; i < SAVE_MAX_LEVELS; i++, p += SAVE_NAME_SIZE + 12) {
        get_name(save->names[i], p);
        save->progress[i][0] = get_int(p + SAVE_NAME_SIZE);
        save->progress[i][1] = get_int(p + SAVE_NAME_SIZE + 4);
        save->progress[i][2] = get_int(p + SAVE_NAME_SIZE + 8);
    }
    free(raw);
    return true;
}

bool save_decode_progress(const unsigned char *data, size_t size, int progress[SAVE_MAX_LEVELS][3])
{
    unsigned char raw[PROGRESS_SIZE];
    int i;

    if (from_text_mode(data, size, raw, sizeof(raw)) < sizeof(raw))
        return false;
    for (i = 0; i < SAVE_MAX_LEVELS; i++) {
        progress[i][0] = get_int(raw + i * 12);
        progress[i][1] = get_int(raw + i * 12 + 4);
        progress[i][2] = get_int(raw + i * 12 + 8);
    }
    return true;
}
