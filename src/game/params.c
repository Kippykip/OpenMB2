/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "params.h"
#include "../core/config.h"

void params_set_defaults(Params *params)
{
    params->sound_volume = 100;
    params->music_volume = 100;
    params->windowed = true;
    params->language = 0;
    params->resolution = 0;
    params->hall_of_fame = false;
    params->infravision = true;
    params->activated = false;
    params->joystick = false;
    params->mouse_sensitivity = 0.75f;
}

bool params_load(Params *params, const char *game_path)
{
    Config cfg;

    params_set_defaults(params);
    if (!config_load(&cfg, game_path))
        return false;

    params->sound_volume = config_get_int(&cfg, "soundvolume", params->sound_volume);
    params->music_volume = config_get_int(&cfg, "musicvolume", params->music_volume);
    params->windowed = config_get_int(&cfg, "window", params->windowed) != 0;
    params->language = config_get_int(&cfg, "language", params->language);
    params->resolution = config_get_int(&cfg, "resolution", params->resolution);
    params->hall_of_fame = config_get_int(&cfg, "hallfame", params->hall_of_fame) != 0;
    params->infravision = config_get_int(&cfg, "infravision", params->infravision) != 0;
    params->activated = config_get_int(&cfg, "act", params->activated) != 0;
    params->joystick = config_get_int(&cfg, "joystick", params->joystick) != 0;
    params->mouse_sensitivity = config_get_float(&cfg, "msensivity", params->mouse_sensitivity);

    config_free(&cfg);
    return true;
}
