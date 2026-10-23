/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * textures.h - the texture set (textureset.txt) and texture scripts.
 *
 * textureset.txt sets the scene lighting and registers texture scripts:
 *
 *     registertexture: Textures/Scripts/t_tileset1.txt
 *
 * Each script names an image in Textures/ and says how to use it:
 *
 *     texture { texture: tileset01.jpg  xfactor: 0.125  yfactor: 0.125
 *               bricktexture  deletedead }
 *
 * Levels refer to scripts by their position in the list (record byte 14),
 * so the order of registertexture lines matters: 0 is t_fake, 1 is
 * t_lighting, 2 is t_water, 4..9 are tileset1..6 in the stock file.
 *
 * Nothing here touches the GPU; the renderer uploads the images.
 */
#ifndef OPENMB2_TEXTURES_H
#define OPENMB2_TEXTURES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TEXFLAG_ALPHA = 1 << 0,        /* alpha blended */
    TEXFLAG_INTERFACE = 1 << 1,    /* 2D interface art */
    TEXFLAG_BRICK = 1 << 2,        /* "bricktexture": used by bricks */
    TEXFLAG_DELETE_DEAD = 1 << 3,  /* "deletedead" */
    TEXFLAG_Z_DISABLE = 1 << 4,    /* "zdisable": no depth writes */
    TEXFLAG_TWO_SIDED = 1 << 5,    /* no back-face culling */
    TEXFLAG_NOT_LIGHTED = 1 << 6,  /* drawn at full brightness */
    TEXFLAG_BILLBOARD = 1 << 7,    /* sprites facing the camera */
    TEXFLAG_SPECULAR = 1 << 8,     /* specular highlights on */
    TEXFLAG_FAKE = 1 << 9,         /* "faketexture": placeholder, no image */
    TEXFLAG_UPDATED = 1 << 10      /* drawn by the game at runtime */
};

typedef struct TextureScript {
    char script[128]; /* the script's game path */
    char image[128];  /* image file name, relative to Textures/ */
    float xfactor, yfactor; /* size of one tile in UV units (1 = whole image) */
    float scalex, scaley;   /* UV scale for scrolling textures (0 = unset) */
    float movex, movey;     /* UV scroll speed */
    uint32_t flags;
} TextureScript;

typedef struct TextureSet {
    float light_dir[3];
    float ambient[4], diffuse[4], specular[4]; /* r, g, b, a */
    TextureScript *scripts;
    int count;
} TextureSet;

/* Load a texture set such as textureset.txt and all the scripts it lists. */
bool textureset_load(TextureSet *set, const char *game_path);
void textureset_free(TextureSet *set);

/* The script at a level's texture index, or NULL if it's out of range. */
const TextureScript *textureset_get(const TextureSet *set, int index);

/*
 * Find the image file for a texture name, the way the game does: the
 * name is looked up in Textures/, and if that file doesn't exist the
 * extension is swapped for .png, .dds, .jpg and .bmp in turn. Models also
 * name board and background images without their subfolder, so
 * Textures/Board/ and Textures/BG/ are searched last. Writes the game
 * path to out and returns false if nothing matches.
 */
bool texture_find_image(const char *name, char *out, size_t out_size);

#endif
