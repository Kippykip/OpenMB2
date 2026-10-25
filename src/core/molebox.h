/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * molebox.h - reading the game's packed data files.
 *
 * Most releases of Magic Ball 2 keep their data in MoleBox boxes rather
 * than in loose folders: Default.pak, Lev0.pak and friends, or
 * MagicBall2.dat on the 2004 disc. The original exe has MoleBox's runtime
 * built in, which shows the boxes' files to the game as if they were on
 * disk. OpenMB2 reads them itself, so those releases work as they are.
 *
 * A box ends with a 44-byte trailer: six numbers (the directory's packed
 * and unpacked size, its offset, the size of its name table, the file
 * count and the box's own size), 16 bytes we don't need, and a 32-bit
 * check that is "MBOX" XORed with 0xcafebabe and the box's size. The
 * directory is encrypted with IDEA, using a key built into each version
 * of the runtime; it holds the names, then 16 bytes per file (name offset,
 * data offset, size, flags). A file is stored in 64 KB chunks, each with
 * a 32-bit header in front of the file's data (the top bit set for a
 * chunk stored as it is), and is either XORed with one byte that is the
 * same for the whole box or, with flag 0x20, encrypted with IDEA too.
 */
#ifndef OPENMB2_MOLEBOX_H
#define OPENMB2_MOLEBOX_H

#include <stdbool.h>
#include <stddef.h>

typedef struct MoleBox MoleBox;

/* Open a box; NULL if the file isn't one this code can read. */
MoleBox *molebox_open(const char *path);
void molebox_close(MoleBox *box);

int molebox_count(const MoleBox *box);
/* A file's path inside the box, in UTF-8 with forward slashes. */
const char *molebox_name(const MoleBox *box, int index);

/* Read a whole file, with a NUL byte added after it like vfs_read.
   Free the result with free(). */
void *molebox_read(const MoleBox *box, int index, size_t *out_size);

#endif
