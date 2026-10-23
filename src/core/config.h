/*
 * config.h - parser for the game's text definition files.
 *
 * Nearly every data file in Magic Ball 2 (boards, pads, prizes, menus,
 * textures, music cues, params.txt) uses the same simple format:
 *
 *     CMenuItemButton          <- optional class name
 *     {
 *       width: 96              <- "key: value"
 *       texture: Options.png
 *       menuitem: Menu/a.txt   <- keys may repeat
 *       bricktexture           <- a bare word is a flag
 *     }
 *
 * Values are kept as the raw text after the colon; the helpers below turn
 * them into numbers, colours and vectors. Key lookups ignore case, because
 * the original files are not consistent about it.
 */
#ifndef OPENMB2_CONFIG_H
#define OPENMB2_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ConfigEntry {
    const char *key;
    const char *value; /* NULL for a flag */
    int line;          /* 1-based line number, for error messages */
} ConfigEntry;

typedef struct Config {
    const char *class_name; /* "" when the file has none */
    ConfigEntry *entries;
    int count;
    char *storage; /* owns all the strings above */
} Config;

/* Parse text into cfg. source_name is only used in warnings. */
bool config_parse(Config *cfg, const char *text, size_t length, const char *source_name);

/* Read and parse a file from the game folder. */
bool config_load(Config *cfg, const char *game_path);

void config_free(Config *cfg);

/* Index of the first entry named key at or after start, or -1. */
int config_find(const Config *cfg, const char *key, int start);

/* True if the key appears at all, as a flag or with a value. */
bool config_has(const Config *cfg, const char *key);

/* Value of the first entry named key, or NULL if missing or a flag. */
const char *config_get(const Config *cfg, const char *key);

/* The same, returning fallback instead of NULL. */
const char *config_get_string(const Config *cfg, const char *key, const char *fallback);

int config_get_int(const Config *cfg, const char *key, int fallback);
float config_get_float(const Config *cfg, const char *key, float fallback);

/* Colours are written as hex ARGB, e.g. "FFFFFF44". */
uint32_t config_get_color(const Config *cfg, const char *key, uint32_t fallback);

/*
 * Read up to max space-separated numbers ("8. 37." -> 8, 37).
 * Returns how many were read; out is untouched past that.
 */
int config_get_floats(const Config *cfg, const char *key, float *out, int max);

/* Parse helpers for a single value string, used by the getters above. */
int config_parse_floats(const char *value, float *out, int max);

#endif
