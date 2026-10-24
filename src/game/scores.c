/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "scores.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY 0x63 /* the byte at 0x48f570 + 0x1ef4 */

static bool get_int(const unsigned char *data, size_t size, size_t *at, int *out)
{
    if (*at + 4 > size)
        return false;
    *out = (int)((uint32_t)data[*at] | (uint32_t)data[*at + 1] << 8 |
                 (uint32_t)data[*at + 2] << 16 | (uint32_t)data[*at + 3] << 24);
    *at += 4;
    return true;
}

static void put_int(unsigned char *p, int value)
{
    uint32_t u = (uint32_t)value;
    p[0] = (unsigned char)u;
    p[1] = (unsigned char)(u >> 8);
    p[2] = (unsigned char)(u >> 16);
    p[3] = (unsigned char)(u >> 24);
}

bool scores_decode(Scores *scores, const unsigned char *data, size_t size)
{
    size_t at = 0;
    int version, check, total = 0, place;

    memset(scores, 0, sizeof(*scores));
    if (!get_int(data, size, &at, &version) || version != 2 || !get_int(data, size, &at, &check))
        return false;
    /* The original reads places until the file ends. */
    for (place = 0; place < SCORES_PLACES && at < size; place++) {
        int length, i;

        if (!get_int(data, size, &at, &length) || length < 0 || (size_t)length > size - at)
            break;
        for (i = 0; i < length; i++) {
            unsigned char c = (unsigned char)(data[at + (size_t)i] ^ (unsigned char)(KEY + i));
            if (i < SCORES_NAME_SIZE - 1)
                scores->names[place][i] = (char)c;
        }
        at += (size_t)length;
        if (!get_int(data, size, &at, &scores->scores[place]))
            break;
        total += scores->scores[place];
    }
    if (total != check) {
        memset(scores, 0, sizeof(*scores));
        return false;
    }
    return true;
}

unsigned char *scores_encode(const Scores *scores, size_t *size)
{
    size_t length = 8, at;
    unsigned char *data;
    int total = 0, place;

    for (place = 0; place < SCORES_PLACES; place++)
        length += 8 + strlen(scores->names[place]);
    data = malloc(length);
    if (data == NULL)
        return NULL;
    put_int(data, 2);
    at = 8;
    for (place = 0; place < SCORES_PLACES; place++) {
        size_t n = strlen(scores->names[place]), i;
        put_int(data + at, (int)n);
        at += 4;
        for (i = 0; i < n; i++)
            data[at + i] = (unsigned char)((unsigned char)scores->names[place][i] ^ (unsigned char)(KEY + i));
        at += n;
        put_int(data + at, scores->scores[place]);
        at += 4;
        total += scores->scores[place];
    }
    put_int(data + 4, total);
    *size = length;
    return data;
}

int scores_count(const Scores *scores)
{
    int place, count = 0;
    for (place = 0; place < SCORES_PLACES; place++) {
        if (scores->scores[place] > 0)
            count = place + 1;
    }
    return count;
}

bool scores_qualifies(const Scores *scores, int score)
{
    return score > scores->scores[SCORES_PLACES - 1];
}

int scores_add(Scores *scores, const char *name, int score)
{
    int place, i;

    for (place = 0; place < SCORES_PLACES; place++) {
        if (score > scores->scores[place])
            break;
    }
    if (place == SCORES_PLACES)
        return -1;
    for (i = SCORES_PLACES - 1; i > place; i--) {
        memcpy(scores->names[i], scores->names[i - 1], SCORES_NAME_SIZE);
        scores->scores[i] = scores->scores[i - 1];
    }
    snprintf(scores->names[place], SCORES_NAME_SIZE, "%s", name);
    scores->scores[place] = score;
    return place;
}
