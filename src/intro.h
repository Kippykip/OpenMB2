/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * intro.h - the Alawar logo that plays between the splash screen and the
 * main menu (the game's state 0x1000): the logo plate spins into a ball,
 * the camera swoops down to a wire floor as the bat slides in, and the
 * Magic Ball title bursts out in a shower of sparks.
 */
#ifndef OPENMB2_INTRO_H
#define OPENMB2_INTRO_H

#include "render/render.h"

#include <stdbool.h>
#include <stdint.h>

/* Load SkinMeshes/LOGO.X and the bat; false if the logo isn't there. */
bool intro_start(void);
/* Draw the next frame (clearing the screen); false once it is over. */
bool intro_frame(int width, int height, uint32_t ms, const RenderLight *light);
/* Free the models; also safe when it never started. */
void intro_stop(void);

#endif
