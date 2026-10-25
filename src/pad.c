/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "pad.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>

#define MAX_PADS 4
#define DEAD_ZONE 0.25f

static struct {
    bool ready;
    bool enabled, vibration;
    SDL_GameController *pads[MAX_PADS];
    SDL_JoystickID ids[MAX_PADS];
    int last;   /* the pad used last, for rumble */
} g_pad;

void pad_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        log_warn("no joypads: %s", SDL_GetError());
        return;
    }
    g_pad.ready = true;
    g_pad.enabled = true;
    g_pad.vibration = true;
    /* Pads already plugged in come as SDL_CONTROLLERDEVICEADDED too. */
}

void pad_shutdown(void)
{
    int i;

    for (i = 0; i < MAX_PADS; i++)
        if (g_pad.pads[i] != NULL)
            SDL_GameControllerClose(g_pad.pads[i]);
    if (g_pad.ready)
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_zero(g_pad);
}

void pad_set_options(bool enabled, bool vibration)
{
    g_pad.enabled = enabled;
    g_pad.vibration = vibration;
}

bool pad_enabled(void)
{
    return g_pad.ready && g_pad.enabled;
}

static int find(SDL_JoystickID id)
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (g_pad.pads[i] != NULL && g_pad.ids[i] == id)
            return i;
    return -1;
}

void pad_handle_event(const SDL_Event *event)
{
    int i;

    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        for (i = 0; i < MAX_PADS && g_pad.pads[i] != NULL; i++)
            ;
        if (i == MAX_PADS)
            break;
        g_pad.pads[i] = SDL_GameControllerOpen(event->cdevice.which);
        if (g_pad.pads[i] == NULL)
            break;
        g_pad.ids[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad.pads[i]));
        if (find(g_pad.ids[i]) != i) { /* opened twice */
            SDL_GameControllerClose(g_pad.pads[i]);
            g_pad.pads[i] = NULL;
            break;
        }
        log_info("joypad: %s", SDL_GameControllerName(g_pad.pads[i]));
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if ((i = find(event->cdevice.which)) >= 0) {
            SDL_GameControllerClose(g_pad.pads[i]);
            g_pad.pads[i] = NULL;
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if ((i = find(event->cbutton.which)) >= 0)
            g_pad.last = i;
        break;
    case SDL_CONTROLLERAXISMOTION:
        if ((i = find(event->caxis.which)) >= 0 && abs(event->caxis.value) > 16000)
            g_pad.last = i;
        break;
    default:
        break;
    }
}

float pad_axis(SDL_GameControllerAxis axis)
{
    float best = 0.0f;
    int i;

    if (!pad_enabled())
        return 0.0f;
    for (i = 0; i < MAX_PADS; i++) {
        float value;
        if (g_pad.pads[i] == NULL)
            continue;
        value = (float)SDL_GameControllerGetAxis(g_pad.pads[i], axis) / 32767.0f;
        if (fabsf(value) > fabsf(best))
            best = value;
    }
    /* Past the dead zone, from 0 again. */
    if (fabsf(best) < DEAD_ZONE)
        return 0.0f;
    best = (best > 0.0f ? best - DEAD_ZONE : best + DEAD_ZONE) / (1.0f - DEAD_ZONE);
    return best > 1.0f ? 1.0f : best < -1.0f ? -1.0f : best;
}

bool pad_button(SDL_GameControllerButton button)
{
    int i;

    if (!pad_enabled())
        return false;
    for (i = 0; i < MAX_PADS; i++)
        if (g_pad.pads[i] != NULL && SDL_GameControllerGetButton(g_pad.pads[i], button))
            return true;
    return false;
}

void pad_rumble(float strength, uint32_t ms)
{
    SDL_GameController *pad;
    Uint16 low, high;

    if (!pad_enabled() || !g_pad.vibration)
        return;
    pad = g_pad.pads[g_pad.last];
    if (pad == NULL)
        return;
    strength = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength;
    /* The heavy motor for the strong ones, the light one for the rest. */
    low = (Uint16)(strength > 0.5f ? strength * 65535.0f : 0.0f);
    high = (Uint16)(strength * 65535.0f);
    SDL_GameControllerRumble(pad, low, high, ms);
}
