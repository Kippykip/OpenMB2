/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * userfile.h - files the game writes in the game folder (saved games,
 * level progress, scores, params.txt), shared with the original.
 *
 * A name is found the way the game data is, whatever its case; a file
 * that doesn't exist yet is made with the name as given.
 */
#ifndef OPENMB2_USERFILE_H
#define OPENMB2_USERFILE_H

#include <stdbool.h>
#include <stddef.h>

/* The whole file, or NULL; free() it. */
unsigned char *userfile_read(const char *name, size_t *size);
bool userfile_write(const char *name, const void *data, size_t size);

/* Where a file of that name is, or would be made. */
void userfile_path(const char *name, char *out, size_t size);

#endif
