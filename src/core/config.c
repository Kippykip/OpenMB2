#include "config.h"
#include "log.h"
#include "vfs.h"

#include <stdlib.h>
#include <string.h>

/* Case-insensitive compare for ASCII keys. */
static bool keys_equal(const char *a, const char *b)
{
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return false;
        if (ca == '\0')
            return true;
    }
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim spaces from both ends of s in place and return the new start. */
static char *trim(char *s)
{
    char *end;

    while (is_space(*s))
        s++;
    end = s + strlen(s);
    while (end > s && is_space(end[-1]))
        *--end = '\0';
    return s;
}

static bool add_entry(Config *cfg, int *capacity, const char *key, const char *value, int line)
{
    if (cfg->count == *capacity) {
        int new_capacity = *capacity ? *capacity * 2 : 16;
        ConfigEntry *grown = realloc(cfg->entries, (size_t)new_capacity * sizeof(*grown));
        if (grown == NULL)
            return false;
        cfg->entries = grown;
        *capacity = new_capacity;
    }
    cfg->entries[cfg->count].key = key;
    cfg->entries[cfg->count].value = value;
    cfg->entries[cfg->count].line = line;
    cfg->count++;
    return true;
}

bool config_parse(Config *cfg, const char *text, size_t length, const char *source_name)
{
    enum { BEFORE_BLOCK, IN_BLOCK, AFTER_BLOCK } state = BEFORE_BLOCK;
    int capacity = 0;
    int line_number = 0;
    char *cursor;

    memset(cfg, 0, sizeof(*cfg));
    cfg->class_name = "";

    /* Work on a private copy, cutting it into strings in place. */
    cfg->storage = malloc(length + 1);
    if (cfg->storage == NULL)
        return false;
    memcpy(cfg->storage, text, length);
    cfg->storage[length] = '\0';

    cursor = cfg->storage;
    while (cursor != NULL && *cursor != '\0') {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        char *colon;

        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor = NULL;
        }
        line_number++;

        line = trim(line);
        if (line[0] == '\0' || (line[0] == '/' && line[1] == '/'))
            continue;

        switch (state) {
        case BEFORE_BLOCK:
            if (strcmp(line, "{") == 0)
                state = IN_BLOCK;
            else if (cfg->class_name[0] == '\0')
                cfg->class_name = line;
            else
                log_warn("%s:%d: unexpected '%s' before '{'", source_name, line_number, line);
            break;

        case IN_BLOCK:
            if (strcmp(line, "}") == 0) {
                state = AFTER_BLOCK;
                break;
            }
            colon = strchr(line, ':');
            if (colon == NULL) {
                /* A bare word is a flag, e.g. "bricktexture". */
                if (!add_entry(cfg, &capacity, line, NULL, line_number))
                    goto out_of_memory;
            } else {
                *colon = '\0';
                if (!add_entry(cfg, &capacity, trim(line), trim(colon + 1), line_number))
                    goto out_of_memory;
            }
            break;

        case AFTER_BLOCK:
            log_warn("%s:%d: ignoring text after '}'", source_name, line_number);
            cursor = NULL;
            break;
        }
    }

    if (state == BEFORE_BLOCK) {
        log_warn("%s: no '{' block found", source_name);
        config_free(cfg);
        return false;
    }
    if (state == IN_BLOCK)
        log_warn("%s: missing closing '}'", source_name);
    return true;

out_of_memory:
    log_error("%s: out of memory", source_name);
    config_free(cfg);
    return false;
}

bool config_load(Config *cfg, const char *game_path)
{
    size_t length;
    char *text = vfs_read(game_path, &length);
    bool ok;

    if (text == NULL) {
        memset(cfg, 0, sizeof(*cfg));
        cfg->class_name = "";
        return false;
    }
    ok = config_parse(cfg, text, length, game_path);
    free(text);
    return ok;
}

void config_free(Config *cfg)
{
    free(cfg->entries);
    free(cfg->storage);
    memset(cfg, 0, sizeof(*cfg));
    cfg->class_name = "";
}

int config_find(const Config *cfg, const char *key, int start)
{
    int i;
    for (i = start < 0 ? 0 : start; i < cfg->count; i++) {
        if (keys_equal(cfg->entries[i].key, key))
            return i;
    }
    return -1;
}

bool config_has(const Config *cfg, const char *key)
{
    return config_find(cfg, key, 0) >= 0;
}

const char *config_get(const Config *cfg, const char *key)
{
    int index = config_find(cfg, key, 0);
    return index >= 0 ? cfg->entries[index].value : NULL;
}

const char *config_get_string(const Config *cfg, const char *key, const char *fallback)
{
    const char *value = config_get(cfg, key);
    return value != NULL ? value : fallback;
}

int config_get_int(const Config *cfg, const char *key, int fallback)
{
    const char *value = config_get(cfg, key);
    char *end;
    long number;

    if (value == NULL)
        return fallback;
    number = strtol(value, &end, 10);
    return end != value ? (int)number : fallback;
}

float config_get_float(const Config *cfg, const char *key, float fallback)
{
    float number;
    return config_get_floats(cfg, key, &number, 1) == 1 ? number : fallback;
}

uint32_t config_get_color(const Config *cfg, const char *key, uint32_t fallback)
{
    const char *value = config_get(cfg, key);
    char *end;
    unsigned long color;

    if (value == NULL)
        return fallback;
    color = strtoul(value, &end, 16);
    return end != value ? (uint32_t)color : fallback;
}

int config_parse_floats(const char *value, float *out, int max)
{
    int count = 0;

    while (value != NULL && count < max) {
        char *end;
        float number = strtof(value, &end);
        if (end == value)
            break;
        out[count++] = number;
        value = end;
    }
    return count;
}

int config_get_floats(const Config *cfg, const char *key, float *out, int max)
{
    return config_parse_floats(config_get(cfg, key), out, max);
}
