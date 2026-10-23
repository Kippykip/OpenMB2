/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "textures.h"
#include "../core/config.h"
#include "../core/log.h"
#include "../core/vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void color_to_floats(uint32_t argb, float out[4])
{
    out[0] = (float)((argb >> 16) & 0xff) / 255.0f;
    out[1] = (float)((argb >> 8) & 0xff) / 255.0f;
    out[2] = (float)(argb & 0xff) / 255.0f;
    out[3] = (float)(argb >> 24) / 255.0f;
}

static const struct {
    const char *word;
    uint32_t flag;
} k_flags[] = {
    { "alpha", TEXFLAG_ALPHA },
    { "interface", TEXFLAG_INTERFACE },
    { "bricktexture", TEXFLAG_BRICK },
    { "deletedead", TEXFLAG_DELETE_DEAD },
    { "zdisable", TEXFLAG_Z_DISABLE },
    { "twosided", TEXFLAG_TWO_SIDED },
    { "notlighted", TEXFLAG_NOT_LIGHTED },
    { "billboard", TEXFLAG_BILLBOARD },
    { "specular", TEXFLAG_SPECULAR },
    { "faketexture", TEXFLAG_FAKE },
    { "updated", TEXFLAG_UPDATED },
};

static void load_script(TextureScript *script, const char *game_path)
{
    Config cfg;
    size_t i;

    memset(script, 0, sizeof(*script));
    snprintf(script->script, sizeof(script->script), "%s", game_path);
    script->xfactor = 1.0f;
    script->yfactor = 1.0f;

    /* A missing script still takes its slot, so later indices stay right. */
    if (!config_load(&cfg, game_path)) {
        script->flags = TEXFLAG_FAKE;
        return;
    }
    snprintf(script->image, sizeof(script->image), "%s", config_get_string(&cfg, "texture", ""));
    script->xfactor = config_get_float(&cfg, "xfactor", 1.0f);
    script->yfactor = config_get_float(&cfg, "yfactor", 1.0f);
    script->scalex = config_get_float(&cfg, "scalex", 0.0f);
    script->scaley = config_get_float(&cfg, "scaley", 0.0f);
    script->movex = config_get_float(&cfg, "movex", 0.0f);
    script->movey = config_get_float(&cfg, "movey", 0.0f);
    for (i = 0; i < sizeof(k_flags) / sizeof(k_flags[0]); i++) {
        if (config_has(&cfg, k_flags[i].word))
            script->flags |= k_flags[i].flag;
    }
    config_free(&cfg);
}

bool textureset_load(TextureSet *set, const char *game_path)
{
    Config cfg;
    float dir[3] = { 0.0f, -1.0f, 0.0f };
    int i = -1;

    memset(set, 0, sizeof(*set));
    if (!config_load(&cfg, game_path))
        return false;

    config_get_floats(&cfg, "lightdir", dir, 3);
    memcpy(set->light_dir, dir, sizeof(dir));
    color_to_floats(config_get_color(&cfg, "ambientcolor", 0xff404040), set->ambient);
    color_to_floats(config_get_color(&cfg, "diffusecolor", 0xffffffff), set->diffuse);
    color_to_floats(config_get_color(&cfg, "specularcolor", 0xff000000), set->specular);

    while ((i = config_find(&cfg, "registertexture", i + 1)) >= 0) {
        TextureScript *grown;
        if (cfg.entries[i].value == NULL)
            continue;
        grown = realloc(set->scripts, (size_t)(set->count + 1) * sizeof(*grown));
        if (grown == NULL)
            break;
        set->scripts = grown;
        load_script(&set->scripts[set->count++], cfg.entries[i].value);
    }

    config_free(&cfg);
    log_debug("%s: %d texture scripts", game_path, set->count);
    return true;
}

void textureset_free(TextureSet *set)
{
    free(set->scripts);
    memset(set, 0, sizeof(*set));
}

const TextureScript *textureset_get(const TextureSet *set, int index)
{
    if (index < 0 || index >= set->count)
        return NULL;
    return &set->scripts[index];
}

/* Try folder/stem.ext for each extension the game knows. */
static bool try_extensions(const char *folder, const char *stem, char *out, size_t out_size)
{
    static const char *const extensions[] = { ".png", ".dds", ".jpg", ".bmp" };
    size_t i;

    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        snprintf(out, out_size, "%s/%s%s", folder, stem, extensions[i]);
        if (vfs_exists(out))
            return true;
    }
    return false;
}

bool texture_find_image(const char *name, char *out, size_t out_size)
{
    static const char *const folders[] = { "Textures", "Textures/Board", "Textures/BG" };
    char stem[128];
    char *dot;
    size_t i;

    if (name == NULL || name[0] == '\0')
        return false;

    snprintf(out, out_size, "Textures/%s", name);
    if (vfs_exists(out))
        return true;

    snprintf(stem, sizeof(stem), "%s", name);
    dot = strrchr(stem, '.');
    if (dot != NULL && strchr(dot, '/') == NULL)
        *dot = '\0';
    for (i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        if (try_extensions(folders[i], stem, out, out_size))
            return true;
    }
    return false;
}
