/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * crash.h - when the game crashes, say where.
 *
 * The report goes to the console (log.txt on the handhelds) and to a file,
 * openmb2_crash.txt in the game folder: what went wrong, then the code
 * addresses on the stack as module+offset. The offsets are looked up in
 * the matching build (addr2line -e openmb2.exe 0x140000000+offset on
 * Windows, addr2line -e openmb2 offset elsewhere).
 */
#ifndef OPENMB2_CRASH_H
#define OPENMB2_CRASH_H

/* Catch crashes from now on and write the report to path. */
void crash_install(const char *path);

#endif
