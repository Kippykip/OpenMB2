/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * des3.h - Triple DES (EDE) in ECB mode, as libmcrypt's "tripledes" does
 * it: the online scores' requests are sent encrypted this way.
 */
#ifndef OPENMB2_DES3_H
#define OPENMB2_DES3_H

#include <stdbool.h>
#include <stddef.h>

/* Encrypt size bytes in place, 8 at a time (a tail under 8 is left).
   A key shorter than 24 bytes is padded with zeros, like libmcrypt. */
void des3_ecb_encrypt(const unsigned char *key, size_t key_size, unsigned char *data, size_t size);

#endif
