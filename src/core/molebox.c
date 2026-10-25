/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "molebox.h"
#include "log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define TRAILER_SIZE 44
#define CHUNK_SIZE   0x10000
#define FLAG_IDEA    0x20
#define CHUNK_STORED 0x80000000u

/* The keys of the MoleBox runtimes the game shipped with: the 2004 disc's
   (MagicBall2.dat) and the 2005 builds' (every .pak). */
static const uint8_t k_keys[][16] = {
    { 0xc1, 0x4c, 0x1d, 0x8c, 0x37, 0xab, 0x94, 0x23,
      0x3d, 0xfa, 0x68, 0x12, 0x80, 0xa3, 0x45, 0x97 },
    { 0xe9, 0x3a, 0xca, 0x69, 0xe2, 0x2a, 0x76, 0xc0,
      0x87, 0x2f, 0xa0, 0x2e, 0xf6, 0x73, 0x65, 0xf0 }
};

typedef struct MoleFile {
    char *name;
    uint32_t offset; /* of the data, after the chunk headers */
    uint32_t size;
    uint32_t flags;
} MoleFile;

struct MoleBox {
    char *path;
    MoleFile *files;
    int count;
    uint16_t key[52];  /* IDEA decryption subkeys */
    uint8_t xor_byte;
};

/* ------------------------------------------------------------------------ */
/* IDEA                                                                     */
/* ------------------------------------------------------------------------ */

/* Multiplication modulo 65537, where 0 stands for 65536. */
static uint16_t idea_mul(uint32_t a, uint32_t b)
{
    if (a == 0)
        a = 0x10000;
    if (b == 0)
        b = 0x10000;
    return (uint16_t)((uint64_t)a * b % 0x10001);
}

static uint16_t idea_inverse(uint16_t x)
{
    uint32_t result = 1, base = x, power = 0x10001 - 2;

    if (x <= 1)
        return x;
    while (power != 0) {
        if (power & 1)
            result = (uint32_t)((uint64_t)result * base % 0x10001);
        base = (uint32_t)((uint64_t)base * base % 0x10001);
        power >>= 1;
    }
    return (uint16_t)result;
}

/* The decryption subkeys for a 128-bit key. */
static void idea_key(const uint8_t key[16], uint16_t out[52])
{
    uint16_t enc[52];
    uint8_t k[16];
    int i, r;

    memcpy(k, key, sizeof(k));
    for (i = 0; i < 52; i++) {
        if (i > 0 && i % 8 == 0) {
            /* Rotate the key left by 25 bits. */
            uint8_t t[16];
            int b;
            for (b = 0; b < 16; b++)
                t[b] = (uint8_t)(k[(b + 3) % 16] << 1 | k[(b + 4) % 16] >> 7);
            memcpy(k, t, sizeof(k));
        }
        enc[i] = (uint16_t)(k[i % 8 * 2] << 8 | k[i % 8 * 2 + 1]);
    }

    out[0] = idea_inverse(enc[48]);
    out[1] = (uint16_t)-enc[49];
    out[2] = (uint16_t)-enc[50];
    out[3] = idea_inverse(enc[51]);
    out[4] = enc[46];
    out[5] = enc[47];
    for (r = 1; r < 8; r++) {
        int at = r * 6, from = 48 - r * 6;
        out[at] = idea_inverse(enc[from]);
        out[at + 1] = (uint16_t)-enc[from + 2];
        out[at + 2] = (uint16_t)-enc[from + 1];
        out[at + 3] = idea_inverse(enc[from + 3]);
        out[at + 4] = enc[from - 2];
        out[at + 5] = enc[from - 1];
    }
    out[48] = idea_inverse(enc[0]);
    out[49] = (uint16_t)-enc[1];
    out[50] = (uint16_t)-enc[2];
    out[51] = idea_inverse(enc[3]);
}

/* Decrypt whole 8-byte blocks in place; a shorter tail is left as it is. */
static void idea_decrypt(const uint16_t key[52], uint8_t *data, size_t size)
{
    size_t at;

    for (at = 0; at + 8 <= size; at += 8) {
        uint8_t *b = data + at;
        uint16_t x1 = (uint16_t)(b[0] << 8 | b[1]), x2 = (uint16_t)(b[2] << 8 | b[3]);
        uint16_t x3 = (uint16_t)(b[4] << 8 | b[5]), x4 = (uint16_t)(b[6] << 8 | b[7]);
        const uint16_t *k = key;
        uint16_t out[4];
        int r;

        for (r = 0; r < 8; r++, k += 6) {
            uint16_t t0, t1;
            x1 = idea_mul(x1, k[0]);
            x2 = (uint16_t)(x2 + k[1]);
            x3 = (uint16_t)(x3 + k[2]);
            x4 = idea_mul(x4, k[3]);
            t0 = idea_mul(k[4], (uint16_t)(x1 ^ x3));
            t1 = idea_mul(k[5], (uint16_t)(t0 + (x2 ^ x4)));
            t0 = (uint16_t)(t0 + t1);
            x1 ^= t1;
            x4 ^= t0;
            t0 ^= x2;
            x2 = (uint16_t)(x3 ^ t1);
            x3 = t0;
        }
        /* The last half round, undoing the last round's swap. */
        out[0] = idea_mul(x1, k[0]);
        out[1] = (uint16_t)(x3 + k[1]);
        out[2] = (uint16_t)(x2 + k[2]);
        out[3] = idea_mul(x4, k[3]);
        for (r = 0; r < 4; r++) {
            b[2 * r] = (uint8_t)(out[r] >> 8);
            b[2 * r + 1] = (uint8_t)out[r];
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static FILE *open_file(const char *path)
{
#ifdef _WIN32
    wchar_t wide[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1024) <= 0)
        return NULL;
    return _wfopen(wide, L"rb");
#else
    return fopen(path, "rb");
#endif
}

static bool read_at(FILE *file, long offset, void *out, size_t size)
{
    return fseek(file, offset, SEEK_SET) == 0 && fread(out, 1, size, file) == size;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Names are in the Windows Cyrillic code page (Levels0r has "062_К1"). */
static const uint16_t k_cp1251[64] = {
    0x0402, 0x0403, 0x201a, 0x0453, 0x201e, 0x2026, 0x2020, 0x2021,
    0x20ac, 0x2030, 0x0409, 0x2039, 0x040a, 0x040c, 0x040b, 0x040f,
    0x0452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0459, 0x203a, 0x045a, 0x045c, 0x045b, 0x045f,
    0x00a0, 0x040e, 0x045e, 0x0408, 0x00a4, 0x0490, 0x00a6, 0x00a7,
    0x0401, 0x00a9, 0x0404, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x0407,
    0x00b0, 0x00b1, 0x0406, 0x0456, 0x0491, 0x00b5, 0x00b6, 0x00b7,
    0x0451, 0x2116, 0x0454, 0x00bb, 0x0458, 0x0405, 0x0455, 0x0457
};

static char *name_to_utf8(const uint8_t *name, size_t length)
{
    char *out = malloc(length * 3 + 1);
    size_t i, n = 0;

    if (out == NULL)
        return NULL;
    for (i = 0; i < length; i++) {
        unsigned c = name[i];
        if (c >= 0x80)
            c = c >= 0xc0 ? 0x0410 + (c - 0xc0) : k_cp1251[c - 0x80];
        if (c == '\\')
            c = '/';
        if (c < 0x80) {
            out[n++] = (char)c;
        } else if (c < 0x800) {
            out[n++] = (char)(0xc0 | c >> 6);
            out[n++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[n++] = (char)(0xe0 | c >> 12);
            out[n++] = (char)(0x80 | (c >> 6 & 0x3f));
            out[n++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[n] = '\0';
    return out;
}

/* ------------------------------------------------------------------------ */
/* Opening a box                                                            */
/* ------------------------------------------------------------------------ */

/* Decrypt the directory with each known key until one gives sensible
   names: a NUL-terminated table of printable text. */
static uint8_t *read_directory(MoleBox *box, FILE *file, long base, const uint32_t info[6])
{
    uint32_t size = info[0], names = info[3];
    uint8_t *encrypted, *plain;
    size_t k;

    if (size == 0 || size > 0x4000000 || names == 0 || names >= size ||
        (uint64_t)info[4] * 16 > size - names)
        return NULL;
    encrypted = malloc(size);
    plain = malloc(size);
    if (encrypted == NULL || plain == NULL || !read_at(file, base + (long)info[2], encrypted, size)) {
        free(encrypted);
        free(plain);
        return NULL;
    }
    for (k = 0; k < sizeof(k_keys) / sizeof(k_keys[0]); k++) {
        uint32_t i;
        bool sensible;

        idea_key(k_keys[k], box->key);
        memcpy(plain, encrypted, size);
        idea_decrypt(box->key, plain, size);
        sensible = plain[names - 1] == '\0';
        for (i = 0; i < names && sensible; i++)
            sensible = plain[i] == '\0' || plain[i] >= 0x20;
        if (sensible) {
            free(encrypted);
            return plain;
        }
    }
    free(encrypted);
    free(plain);
    return NULL;
}

/* The byte the box's plain files are XORed with. MoleBox picks it per
   build (0x88 on the disc, 0x21 in the paks), so guess it from the first
   bytes of files whose format we know, or failing that from text. */
static uint8_t guess_xor(const MoleBox *box, FILE *file)
{
    static const struct { const char *ext; uint8_t first; } known[] = {
        { ".png", 0x89 }, { ".mb2", 'M' }, { ".ogg", 'O' }, { ".jpg", 0xff },
        { ".x", 'x' }, { ".bmp", 'B' }, { ".dds", 'D' }
    };
    int votes[256] = { 0 }, best = 0, i;
    int text = -1;

    for (i = 0; i < box->count; i++) {
        const MoleFile *f = &box->files[i];
        const char *dot = strrchr(f->name, '.');
        uint8_t first;
        size_t e;

        if ((f->flags & FLAG_IDEA) || f->size == 0 || dot == NULL)
            continue;
        if (text < 0)
            text = i;
        /* Extensions compare without regard to case. */
        for (e = 0; e < sizeof(known) / sizeof(known[0]); e++) {
            const char *a = dot, *b = known[e].ext;
            while (*a != '\0' && *b != '\0' && (*a | 0x20) == *b)
                a++, b++;
            if (*a == '\0' && *b == '\0')
                break;
        }
        if (e < sizeof(known) / sizeof(known[0]) && read_at(file, (long)f->offset, &first, 1))
            votes[first ^ known[e].first]++;
    }
    for (i = 1; i < 256; i++)
        if (votes[i] > votes[best])
            best = i;
    if (votes[best] > 0 || text < 0)
        return (uint8_t)best;

    /* No file we know: take the byte that turns the most into text. */
    {
        uint8_t sample[256];
        size_t n = box->files[text].size < sizeof(sample) ? box->files[text].size : sizeof(sample);
        int score_best = -1, x;
        if (!read_at(file, (long)box->files[text].offset, sample, n))
            return 0;
        for (x = 0; x < 256; x++) {
            int score = 0;
            size_t j;
            for (j = 0; j < n; j++) {
                uint8_t c = (uint8_t)(sample[j] ^ x);
                score += (c >= 0x20 && c < 0x7f) || c == '\r' || c == '\n' || c == '\t';
            }
            if (score > score_best) {
                score_best = score;
                best = x;
            }
        }
        return (uint8_t)best;
    }
}

MoleBox *molebox_open(const char *path)
{
    uint8_t trailer[TRAILER_SIZE], *directory;
    uint32_t info[6], check;
    MoleBox *box;
    FILE *file;
    long length, base;
    int i;

    file = open_file(path);
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < TRAILER_SIZE ||
        !read_at(file, length - TRAILER_SIZE, trailer, sizeof(trailer))) {
        fclose(file);
        return NULL;
    }
    check = get32(trailer + 40) ^ 0xcafebabeu ^ (uint32_t)length;
    if (check != 0x584f424du) { /* "MBOX" */
        fclose(file);
        return NULL;
    }
    for (i = 0; i < 6; i++)
        info[i] = get32(trailer + 4 * i);
    /* A box can sit at the end of another file; its offsets count from
       its own start. */
    base = length - (long)info[5];

    box = calloc(1, sizeof(*box));
    directory = box != NULL ? read_directory(box, file, base, info) : NULL;
    if (directory == NULL) {
        log_warn("molebox: can't read '%s' (an unknown MoleBox version?)", path);
        free(box);
        fclose(file);
        return NULL;
    }

    box->path = malloc(strlen(path) + 1);
    box->files = calloc(info[4] != 0 ? info[4] : 1, sizeof(MoleFile));
    if (box->path != NULL)
        strcpy(box->path, path);
    for (i = 0; box->path != NULL && box->files != NULL && i < (int)info[4]; i++) {
        const uint8_t *entry = directory + info[3] + 16 * i;
        uint32_t name = get32(entry);
        const uint8_t *end;
        MoleFile *f = &box->files[box->count];

        if (name >= info[3])
            continue;
        end = memchr(directory + name, '\0', info[3] - name);
        if (end == NULL)
            continue;
        f->name = name_to_utf8(directory + name, (size_t)(end - (directory + name)));
        f->offset = (uint32_t)base + get32(entry + 4);
        f->size = get32(entry + 8);
        f->flags = get32(entry + 12);
        if (f->name != NULL)
            box->count++;
    }
    free(directory);
    box->xor_byte = guess_xor(box, file);
    fclose(file);
    log_info("molebox: '%s' has %d files", path, box->count);
    return box;
}

void molebox_close(MoleBox *box)
{
    int i;

    if (box == NULL)
        return;
    for (i = 0; i < box->count; i++)
        free(box->files[i].name);
    free(box->files);
    free(box->path);
    free(box);
}

int molebox_count(const MoleBox *box)
{
    return box != NULL ? box->count : 0;
}

const char *molebox_name(const MoleBox *box, int index)
{
    return box != NULL && index >= 0 && index < box->count ? box->files[index].name : NULL;
}

void *molebox_read(const MoleBox *box, int index, size_t *out_size)
{
    const MoleFile *f;
    uint32_t chunks, i, done = 0, *headers;
    uint8_t *data;
    long at;
    FILE *file;

    if (out_size != NULL)
        *out_size = 0;
    if (box == NULL || index < 0 || index >= box->count)
        return NULL;
    f = &box->files[index];
    chunks = (f->size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    if ((file = open_file(box->path)) == NULL)
        return NULL;

    headers = malloc((chunks != 0 ? chunks : 1) * sizeof(uint32_t));
    data = malloc((size_t)f->size + 1);
    if (headers == NULL || data == NULL)
        goto fail;
    for (i = 0; i < chunks; i++) {
        uint8_t b[4];
        if (!read_at(file, (long)(f->offset - 4 * (chunks - i)), b, 4))
            goto fail;
        headers[i] = get32(b);
    }

    at = (long)f->offset;
    for (i = 0; i < chunks; i++) {
        uint32_t stored = headers[i] & ~CHUNK_STORED;
        uint32_t want = f->size - done < CHUNK_SIZE ? f->size - done : CHUNK_SIZE;
        uint32_t j;

        /* Every box the game shipped stores its chunks; MoleBox can also
           compress them, which this doesn't read. */
        if (!(headers[i] & CHUNK_STORED) || stored != want) {
            log_warn("molebox: '%s' in '%s' is compressed, which isn't supported", f->name, box->path);
            goto fail;
        }
        if (!read_at(file, at, data + done, stored))
            goto fail;
        if (f->flags & FLAG_IDEA)
            idea_decrypt(box->key, data + done, stored);
        else
            for (j = 0; j < stored; j++)
                data[done + j] ^= box->xor_byte;
        at += (long)stored;
        done += stored;
    }
    fclose(file);
    free(headers);
    data[f->size] = '\0';
    if (out_size != NULL)
        *out_size = f->size;
    return data;

fail:
    fclose(file);
    free(headers);
    free(data);
    return NULL;
}
