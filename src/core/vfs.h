/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * vfs.h - read-only access to the user's Magic Ball 2 game folder.
 *
 * The original game was written for Windows, so its data files refer to
 * each other with any mix of upper/lower case and back or forward slashes
 * ("Boards/boardround1.txt", "Menu\\LMAINMENU.TXT", ...). Other platforms
 * have case-sensitive file systems, so we index the whole folder once at
 * mount time and look names up case-insensitively.
 *
 * Game paths are always relative to the game folder, e.g. "Menu/exit.txt".
 */
#ifndef OPENMB2_VFS_H
#define OPENMB2_VFS_H

#include <stdbool.h>
#include <stddef.h>

/* Index the folder at root_dir. Returns false if it can't be read. */
bool vfs_mount(const char *root_dir);
void vfs_unmount(void);

/* The folder passed to vfs_mount, or NULL when nothing is mounted. */
const char *vfs_root(void);

/* Number of files (not folders) in the index. */
int vfs_file_count(void);

bool vfs_exists(const char *game_path);
bool vfs_is_dir(const char *game_path);

/*
 * Turn a game path into a real path on disk, with the correct case.
 * Returns false if the file doesn't exist or out is too small.
 */
bool vfs_real_path(const char *game_path, char *out, size_t out_size);

/*
 * Read a whole file into memory. A NUL byte is added after the data, so
 * text files can be used as C strings. Free the result with free().
 * Returns NULL if the file is missing or can't be read.
 */
void *vfs_read(const char *game_path, size_t *out_size);

/*
 * Call fn for every direct child of a folder ("" for the game folder
 * itself). name is the child's real name, without the folder part.
 * Returns the number of children visited.
 */
typedef void (*VfsListFn)(const char *name, bool is_dir, void *user);
int vfs_list(const char *game_dir, VfsListFn fn, void *user);

/*
 * Normalise a game path: backslashes become slashes, repeated and leading
 * slashes and "./" are removed, and ASCII letters are lowercased.
 */
void vfs_normalise(const char *game_path, char *out, size_t out_size);

#endif
