/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * pad.h - joypads (OpenMB2's own; the original only knew the mouse).
 *
 * SDL's game controllers: Xbox pads through XInput or Windows.Gaming.Input,
 * PlayStation and Switch pads, and most others, all with the same button
 * names. Whichever pad was used last is the one that rumbles.
 */
#ifndef OPENMB2_PAD_H
#define OPENMB2_PAD_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

void pad_init(void);
void pad_shutdown(void);
/* The joystick settings: off, the pads are ignored. */
void pad_set_options(bool enabled, bool vibration);
/* Pads plugged in and pulled out. */
void pad_handle_event(const SDL_Event *event);
bool pad_enabled(void);
/* A stick, -1 to 1 past a dead zone (right and down are positive);
   the strongest of the pads. 0 while pads are off. */
float pad_axis(SDL_GameControllerAxis axis);
/* Held on any pad. */
bool pad_button(SDL_GameControllerButton button);
/* Shake the last pad used: 0 to 1, for so many ms. */
void pad_rumble(float strength, uint32_t ms);

#endif
