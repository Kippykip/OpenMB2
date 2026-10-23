/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "audio.h"
#include "../core/log.h"
#include "../core/vfs.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* From stb_vorbis_impl.c. */
int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate,
                             short **output);

#define OUTPUT_RATE 44100
#define MAX_VOICES  24
#define MAX_SOUNDS  128

typedef struct Sound {
    char name[64];
    short *samples; /* mono or stereo, interleaved */
    int channels, rate, frames;
} Sound;

/* A sound that is playing. position is in the sound's own frames, 16.16. */
typedef struct Voice {
    const Sound *sound;
    uint32_t position, step;
    int left, right; /* gains, 0-256 */
} Voice;

static struct {
    SDL_AudioDeviceID device;
    int master; /* 0-256 */
    Sound sounds[MAX_SOUNDS];
    int sound_count;
    Voice voices[MAX_VOICES];
} a;

static short sample_at(const Sound *s, int frame, int channel)
{
    if (frame >= s->frames)
        return 0;
    return s->samples[frame * s->channels + (s->channels > 1 ? channel : 0)];
}

static void mix(void *user, Uint8 *stream, int bytes)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = bytes / 4, i, v;

    (void)user;
    for (i = 0; i < frames; i++) {
        int left = 0, right = 0;

        for (v = 0; v < MAX_VOICES; v++) {
            Voice *voice = &a.voices[v];
            int frame, l, r;
            uint32_t frac;

            if (voice->sound == NULL)
                continue;
            frame = (int)(voice->position >> 16);
            if (frame >= voice->sound->frames) {
                voice->sound = NULL;
                continue;
            }
            /* Linear interpolation, for sounds that aren't at 44.1 kHz. */
            frac = voice->position & 0xffff;
            l = sample_at(voice->sound, frame, 0);
            r = sample_at(voice->sound, frame, 1);
            l += (int)(((int64_t)(sample_at(voice->sound, frame + 1, 0) - l) * frac) >> 16);
            r += (int)(((int64_t)(sample_at(voice->sound, frame + 1, 1) - r) * frac) >> 16);
            left += (l * voice->left) >> 8;
            right += (r * voice->right) >> 8;
            voice->position += voice->step;
        }
        left = (left * a.master) >> 8;
        right = (right * a.master) >> 8;
        out[i * 2] = (Sint16)(left > 32767 ? 32767 : left < -32768 ? -32768 : left);
        out[i * 2 + 1] = (Sint16)(right > 32767 ? 32767 : right < -32768 ? -32768 : right);
    }
}

bool audio_init(int sound_volume)
{
    SDL_AudioSpec want, have;

    memset(&a, 0, sizeof(a));
    a.master = sound_volume * 256 / 100;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        log_warn("no sound: %s", SDL_GetError());
        return false;
    }
    memset(&want, 0, sizeof(want));
    want.freq = OUTPUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    a.device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (a.device == 0) {
        log_warn("no sound: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(a.device, 0);
    return true;
}

void audio_shutdown(void)
{
    int i;

    if (a.device != 0)
        SDL_CloseAudioDevice(a.device);
    for (i = 0; i < a.sound_count; i++)
        free(a.sounds[i].samples);
    memset(&a, 0, sizeof(a));
}

static const Sound *find_sound(const char *name)
{
    char path[96];
    Sound *s;
    void *data;
    size_t size;
    int i;

    for (i = 0; i < a.sound_count; i++) {
        if (SDL_strcasecmp(a.sounds[i].name, name) == 0)
            return a.sounds[i].samples != NULL ? &a.sounds[i] : NULL;
    }
    if (a.sound_count >= MAX_SOUNDS)
        return NULL;

    /* Remember failures too, so a missing file is only reported once. */
    s = &a.sounds[a.sound_count++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    snprintf(path, sizeof(path), "Sounds/%s", name);
    data = vfs_read(path, &size);
    if (data == NULL) {
        log_warn("sound %s not found", path);
        return NULL;
    }
    s->frames = stb_vorbis_decode_memory(data, (int)size, &s->channels, &s->rate, &s->samples);
    free(data);
    if (s->frames <= 0 || s->samples == NULL) {
        log_warn("can't decode %s", path);
        s->samples = NULL;
        return NULL;
    }
    return s;
}

void audio_play(const char *name, int volume, int pan)
{
    const Sound *sound;
    Voice voice;
    int v;

    if (a.device == 0)
        return;
    sound = find_sound(name);
    if (sound == NULL)
        return;
    if (volume < 0 || volume > 100)
        volume = 100;
    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;

    voice.sound = sound;
    voice.position = 0;
    voice.step = (uint32_t)(((uint64_t)sound->rate << 16) / OUTPUT_RATE);
    /* DirectSound-style panning: the far side is turned down, the near side stays. */
    voice.left = volume * 256 / 100 * (pan > 0 ? 100 - pan : 100) / 100;
    voice.right = volume * 256 / 100 * (pan < 0 ? 100 + pan : 100) / 100;

    SDL_LockAudioDevice(a.device);
    for (v = 0; v < MAX_VOICES; v++) {
        if (a.voices[v].sound == NULL) {
            a.voices[v] = voice;
            break;
        }
    }
    SDL_UnlockAudioDevice(a.device);
}
