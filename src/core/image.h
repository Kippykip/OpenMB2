/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * image.h - decode the game's texture files into RGBA pixels.
 *
 * Handles .jpg, .png, .bmp and .tga (through stb_image) and .dds with
 * DXT1/DXT3/DXT5 or plain 32-bit pixels, which covers every texture the
 * game ships. DDS is decoded in software because GLES2 devices don't all
 * support compressed textures.
 */
#ifndef OPENMB2_IMAGE_H
#define OPENMB2_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Image {
    int width, height;
    uint8_t *pixels; /* width * height * 4 bytes, RGBA, top row first */
} Image;

bool image_decode(Image *image, const void *data, size_t size, const char *name);
bool image_load(Image *image, const char *game_path);
void image_free(Image *image);

/*
 * Direct3D 8's texture loader stretches every image to power-of-two sides
 * (D3DX_DEFAULT size), so the game's UVs assume that. GLES2 also needs
 * power-of-two sides to repeat and mipmap, so we do the same.
 */
bool image_resize_to_power_of_two(Image *image);

#endif
