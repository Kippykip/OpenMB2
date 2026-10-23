/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "image.h"
#include "log.h"
#include "vfs.h"

#include "../third_party/stb_image.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* DDS                                                                      */
/* ------------------------------------------------------------------------ */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Expand a 5:6:5 colour to 8 bits per channel. */
static void unpack_565(uint32_t c, uint8_t out[4])
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = (uint8_t)(r << 3 | r >> 2);
    out[1] = (uint8_t)(g << 2 | g >> 4);
    out[2] = (uint8_t)(b << 3 | b >> 2);
    out[3] = 255;
}

/* Decode the 8-byte colour half of a DXT block into 16 RGBA pixels. */
static void decode_color_block(const uint8_t *block, bool allow_transparent, uint8_t out[16][4])
{
    uint32_t c0 = (uint32_t)block[0] | (uint32_t)block[1] << 8;
    uint32_t c1 = (uint32_t)block[2] | (uint32_t)block[3] << 8;
    uint32_t bits = le32(block + 4);
    uint8_t palette[4][4];
    int i, k;

    unpack_565(c0, palette[0]);
    unpack_565(c1, palette[1]);
    if (c0 > c1 || !allow_transparent) {
        for (k = 0; k < 3; k++) {
            palette[2][k] = (uint8_t)((2 * palette[0][k] + palette[1][k]) / 3);
            palette[3][k] = (uint8_t)((palette[0][k] + 2 * palette[1][k]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (k = 0; k < 3; k++)
            palette[2][k] = (uint8_t)((palette[0][k] + palette[1][k]) / 2);
        palette[2][3] = 255;
        memset(palette[3], 0, 4); /* transparent black */
    }
    for (i = 0; i < 16; i++)
        memcpy(out[i], palette[(bits >> (2 * i)) & 3], 4);
}

/* DXT5 alpha: two endpoints and sixteen 3-bit indices. */
static void decode_dxt5_alpha(const uint8_t *block, uint8_t out[16][4])
{
    uint32_t a0 = block[0], a1 = block[1];
    uint8_t alpha[8];
    uint64_t bits = 0;
    int i;

    for (i = 0; i < 6; i++)
        bits |= (uint64_t)block[2 + i] << (8 * i);
    alpha[0] = (uint8_t)a0;
    alpha[1] = (uint8_t)a1;
    if (a0 > a1) {
        for (i = 1; i <= 6; i++)
            alpha[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
    } else {
        for (i = 1; i <= 4; i++)
            alpha[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }
    for (i = 0; i < 16; i++)
        out[i][3] = alpha[(bits >> (3 * i)) & 7];
}

static int mask_shift(uint32_t mask)
{
    int shift = 0;
    if (mask == 0)
        return 0;
    while (!(mask & 1)) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static uint8_t extract_channel(uint32_t pixel, uint32_t mask)
{
    uint32_t value, max;
    if (mask == 0)
        return 255;
    value = (pixel & mask) >> mask_shift(mask);
    max = mask >> mask_shift(mask);
    return (uint8_t)(value * 255 / max);
}

static bool decode_dds(Image *image, const uint8_t *data, size_t size, const char *name)
{
    enum { HEADER = 128 };
    uint32_t height, width, flags, fourcc, bit_count;
    int x, y;

    if (size < HEADER || le32(data + 4) != 124) {
        log_error("%s: bad DDS header", name);
        return false;
    }
    height = le32(data + 12);
    width = le32(data + 16);
    flags = le32(data + 80);
    fourcc = le32(data + 84);
    bit_count = le32(data + 88);
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        log_error("%s: bad DDS size", name);
        return false;
    }

    image->width = (int)width;
    image->height = (int)height;
    image->pixels = malloc((size_t)width * height * 4);
    if (image->pixels == NULL)
        return false;

    if (flags & 0x4) { /* DDPF_FOURCC: compressed */
        int block_size;
        const uint8_t *block = data + HEADER;
        bool dxt1 = fourcc == 0x31545844, dxt3 = fourcc == 0x33545844,
             dxt5 = fourcc == 0x35545844;

        if (!dxt1 && !dxt3 && !dxt5) {
            log_error("%s: unsupported DDS format %.4s", name, (const char *)(data + 84));
            image_free(image);
            return false;
        }
        block_size = dxt1 ? 8 : 16;
        if (size < HEADER + (size_t)((width + 3) / 4) * ((height + 3) / 4) * block_size) {
            log_error("%s: DDS file is cut short", name);
            image_free(image);
            return false;
        }

        for (y = 0; y < (int)height; y += 4) {
            for (x = 0; x < (int)width; x += 4, block += block_size) {
                uint8_t texels[16][4];
                int i;

                decode_color_block(dxt1 ? block : block + 8, dxt1, texels);
                if (dxt3) {
                    for (i = 0; i < 16; i++) {
                        int a = (block[i / 2] >> (4 * (i & 1))) & 15;
                        texels[i][3] = (uint8_t)(a * 17);
                    }
                } else if (dxt5) {
                    decode_dxt5_alpha(block, texels);
                }
                for (i = 0; i < 16; i++) {
                    int px = x + (i & 3), py = y + (i >> 2);
                    if (px < (int)width && py < (int)height)
                        memcpy(&image->pixels[((size_t)py * width + px) * 4], texels[i], 4);
                }
            }
        }
        return true;
    }

    if ((flags & 0x40) && (bit_count == 32 || bit_count == 24)) { /* DDPF_RGB */
        uint32_t r_mask = le32(data + 92), g_mask = le32(data + 96), b_mask = le32(data + 100);
        uint32_t a_mask = (flags & 0x1) ? le32(data + 104) : 0;
        size_t bytes = bit_count / 8;
        const uint8_t *p = data + HEADER;

        if (size < HEADER + (size_t)width * height * bytes) {
            log_error("%s: DDS file is cut short", name);
            image_free(image);
            return false;
        }
        for (y = 0; y < (int)(width * height); y++, p += bytes) {
            uint32_t pixel = bytes == 4 ? le32(p) : ((uint32_t)p[0] | p[1] << 8 | p[2] << 16);
            uint8_t *out = &image->pixels[(size_t)y * 4];
            out[0] = extract_channel(pixel, r_mask);
            out[1] = extract_channel(pixel, g_mask);
            out[2] = extract_channel(pixel, b_mask);
            out[3] = extract_channel(pixel, a_mask);
        }
        return true;
    }

    log_error("%s: unsupported DDS pixel format", name);
    image_free(image);
    return false;
}

/* ------------------------------------------------------------------------ */

bool image_decode(Image *image, const void *data, size_t size, const char *name)
{
    int channels;

    memset(image, 0, sizeof(*image));
    if (size >= 4 && memcmp(data, "DDS ", 4) == 0)
        return decode_dds(image, data, size, name);

    image->pixels = stbi_load_from_memory(data, (int)size, &image->width, &image->height,
                                          &channels, 4);
    if (image->pixels == NULL) {
        log_error("%s: can't decode image (%s)", name, stbi_failure_reason());
        return false;
    }
    return true;
}

bool image_load(Image *image, const char *game_path)
{
    size_t size;
    void *data = vfs_read(game_path, &size);
    bool ok;

    if (data == NULL) {
        memset(image, 0, sizeof(*image));
        log_error("can't read %s", game_path);
        return false;
    }
    ok = image_decode(image, data, size, game_path);
    free(data);
    return ok;
}

void image_free(Image *image)
{
    free(image->pixels); /* stb_image allocates with malloc too */
    memset(image, 0, sizeof(*image));
}

static int next_power_of_two(int n)
{
    int p = 1;
    while (p < n)
        p *= 2;
    return p;
}

bool image_resize_to_power_of_two(Image *image)
{
    int new_width = next_power_of_two(image->width);
    int new_height = next_power_of_two(image->height);
    uint8_t *pixels;
    int x, y, c;

    if (new_width == image->width && new_height == image->height)
        return true;

    pixels = malloc((size_t)new_width * new_height * 4);
    if (pixels == NULL)
        return false;

    /* Bilinear stretch, sampling at pixel centres. */
    for (y = 0; y < new_height; y++) {
        float sy = ((float)y + 0.5f) * (float)image->height / (float)new_height - 0.5f;
        int y0 = sy < 0.0f ? 0 : (int)sy;
        int y1 = y0 + 1 < image->height ? y0 + 1 : image->height - 1;
        float fy = sy < 0.0f ? 0.0f : sy - (float)y0;

        for (x = 0; x < new_width; x++) {
            float sx = ((float)x + 0.5f) * (float)image->width / (float)new_width - 0.5f;
            int x0 = sx < 0.0f ? 0 : (int)sx;
            int x1 = x0 + 1 < image->width ? x0 + 1 : image->width - 1;
            float fx = sx < 0.0f ? 0.0f : sx - (float)x0;
            const uint8_t *p00 = &image->pixels[((size_t)y0 * image->width + x0) * 4];
            const uint8_t *p01 = &image->pixels[((size_t)y0 * image->width + x1) * 4];
            const uint8_t *p10 = &image->pixels[((size_t)y1 * image->width + x0) * 4];
            const uint8_t *p11 = &image->pixels[((size_t)y1 * image->width + x1) * 4];

            for (c = 0; c < 4; c++) {
                float top = p00[c] + (p01[c] - p00[c]) * fx;
                float bottom = p10[c] + (p11[c] - p10[c]) * fx;
                pixels[((size_t)y * new_width + x) * 4 + c] =
                    (uint8_t)(top + (bottom - top) * fy + 0.5f);
            }
        }
    }

    free(image->pixels);
    image->pixels = pixels;
    image->width = new_width;
    image->height = new_height;
    return true;
}
