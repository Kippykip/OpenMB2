/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "audio.h"
#include "../core/log.h"
#include "../core/config.h"
#include "../core/vfs.h"

#include <libopenmpt/libopenmpt.h>

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
    uint64_t position; /* 64 bits: a 32-bit 16.16 count wraps after 65536 frames */
    uint32_t step;
    int left, right; /* gains, 0-256 */
    int volume;      /* 0-100, kept for re-panning */
    int loop;        /* a looping voice's handle, 0 for a one-shot */
} Voice;

static struct {
    SDL_AudioDeviceID device;
    int master; /* 0-256 */
    Sound sounds[MAX_SOUNDS];
    int sound_count;
    Voice voices[MAX_VOICES];
    int next_loop;
    int music_master;          /* 0-256 */
    openmpt_module *music;     /* NULL when no music is playing */
    char music_file[96];       /* the module it was loaded from */
    char music_cue[96];        /* the Music/ file that started it */
    int16_t music_buffer[2048 * 2];
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
    /* The music goes under the sound effects. */
    if (a.music != NULL && frames <= 2048)
        openmpt_module_read_interleaved_stereo(a.music, OUTPUT_RATE, (size_t)frames, a.music_buffer);
    else
        memset(a.music_buffer, 0, sizeof(a.music_buffer));
    for (i = 0; i < frames; i++) {
        int left = (a.music_buffer[i * 2] * a.music_master) >> 8;
        int right = (a.music_buffer[i * 2 + 1] * a.music_master) >> 8;
        int effects_left = 0, effects_right = 0;

        for (v = 0; v < MAX_VOICES; v++) {
            Voice *voice = &a.voices[v];
            int frame, l, r;
            uint32_t frac;

            if (voice->sound == NULL)
                continue;
            frame = (int)(voice->position >> 16);
            if (frame >= voice->sound->frames) {
                if (voice->loop == 0) {
                    voice->sound = NULL;
                    continue;
                }
                voice->position -= (uint64_t)voice->sound->frames << 16;
                frame = (int)(voice->position >> 16);
            }
            /* Linear interpolation, for sounds that aren't at 44.1 kHz. */
            frac = (uint32_t)(voice->position & 0xffff);
            l = sample_at(voice->sound, frame, 0);
            r = sample_at(voice->sound, frame, 1);
            l += (int)(((int64_t)(sample_at(voice->sound, frame + 1, 0) - l) * frac) >> 16);
            r += (int)(((int64_t)(sample_at(voice->sound, frame + 1, 1) - r) * frac) >> 16);
            effects_left += (l * voice->left) >> 8;
            effects_right += (r * voice->right) >> 8;
            voice->position += voice->step;
        }
        left += (effects_left * a.master) >> 8;
        right += (effects_right * a.master) >> 8;
        out[i * 2] = (Sint16)(left > 32767 ? 32767 : left < -32768 ? -32768 : left);
        out[i * 2 + 1] = (Sint16)(right > 32767 ? 32767 : right < -32768 ? -32768 : right);
    }
}

bool audio_init(int sound_volume, int music_volume)
{
    SDL_AudioSpec want, have;

    memset(&a, 0, sizeof(a));
    a.master = sound_volume * 256 / 100;
    a.music_master = music_volume * 256 / 100;
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

void audio_set_volumes(int sound_volume, int music_volume)
{
    if (a.device != 0)
        SDL_LockAudioDevice(a.device);
    a.master = sound_volume * 256 / 100;
    a.music_master = music_volume * 256 / 100;
    if (a.device != 0)
        SDL_UnlockAudioDevice(a.device);
}

void audio_shutdown(void)
{
    int i;

    if (a.device != 0)
        SDL_CloseAudioDevice(a.device);
    if (a.music != NULL)
        openmpt_module_destroy(a.music);
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

/* DirectSound-style panning: the far side is turned down, the near side stays. */
static void set_gains(Voice *voice, int volume, int pan)
{
    if (volume < 0 || volume > 100)
        volume = 100;
    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;
    voice->volume = volume;
    voice->left = volume * 256 / 100 * (pan > 0 ? 100 - pan : 100) / 100;
    voice->right = volume * 256 / 100 * (pan < 0 ? 100 + pan : 100) / 100;
}

static int start_voice(const char *name, int volume, int pan, int loop)
{
    const Sound *sound;
    Voice voice;
    int v;

    if (a.device == 0)
        return 0;
    sound = find_sound(name);
    if (sound == NULL)
        return 0;
    memset(&voice, 0, sizeof(voice));
    voice.sound = sound;
    voice.step = (uint32_t)(((uint64_t)sound->rate << 16) / OUTPUT_RATE);
    voice.loop = loop;
    set_gains(&voice, volume, pan);

    SDL_LockAudioDevice(a.device);
    for (v = 0; v < MAX_VOICES; v++) {
        if (a.voices[v].sound == NULL) {
            a.voices[v] = voice;
            break;
        }
    }
    SDL_UnlockAudioDevice(a.device);
    return v < MAX_VOICES ? loop : 0;
}

void audio_play(const char *name, int volume, int pan)
{
    start_voice(name, volume, pan, 0);
}

int audio_play_loop(const char *name, int volume, int pan)
{
    if (++a.next_loop <= 0)
        a.next_loop = 1;
    return start_voice(name, volume, pan, a.next_loop);
}

void audio_pan_loop(int handle, int pan)
{
    int v;

    if (a.device == 0 || handle == 0)
        return;
    SDL_LockAudioDevice(a.device);
    for (v = 0; v < MAX_VOICES; v++)
        if (a.voices[v].sound != NULL && a.voices[v].loop == handle)
            set_gains(&a.voices[v], a.voices[v].volume, pan);
    SDL_UnlockAudioDevice(a.device);
}

void audio_stop_loop(int handle)
{
    int v;

    if (a.device == 0 || handle == 0)
        return;
    SDL_LockAudioDevice(a.device);
    for (v = 0; v < MAX_VOICES; v++)
        if (a.voices[v].loop == handle)
            a.voices[v].sound = NULL;
    SDL_UnlockAudioDevice(a.device);
}

/* libopenmpt's messages go to our log; only warnings and worse. */
static void music_log(const char *message, void *user)
{
    (void)user;
    log_warn("music: %s", message);
}

static openmpt_module *load_module(const char *path)
{
    openmpt_module *module;
    void *data;
    size_t size;

    data = vfs_read(path, &size);
    if (data == NULL) {
        log_warn("music %s not found", path);
        return NULL;
    }
    module = openmpt_module_create_from_memory2(data, size, music_log, NULL, NULL, NULL, NULL,
                                                NULL, NULL);
    free(data);
    if (module == NULL) {
        log_warn("can't play %s", path);
        return NULL;
    }
    /* Each tune loops back on itself inside the module; never stop. */
    openmpt_module_set_repeat_count(module, -1);
    return module;
}

void audio_play_music(const char *path)
{
    Config cfg;
    const char *file;
    int order, row;
    openmpt_module *module = NULL, *old = NULL;

    if (a.device == 0)
        return;
    if (!config_load(&cfg, path)) {
        log_warn("can't load %s", path);
        return;
    }
    file = config_get_string(&cfg, "file", "");
    order = config_get_int(&cfg, "order", 0);
    row = config_get_int(&cfg, "row", 0);

    /* All the tunes are in one module, so keep it if it's the same file. */
    if (a.music == NULL || SDL_strcasecmp(a.music_file, file) != 0)
        module = load_module(file);
    SDL_LockAudioDevice(a.device);
    if (module != NULL) {
        old = a.music;
        a.music = module;
        snprintf(a.music_file, sizeof(a.music_file), "%s", file);
    }
    snprintf(a.music_cue, sizeof(a.music_cue), "%s", path);
    if (a.music != NULL)
        openmpt_module_set_position_order_row(a.music, order, row);
    SDL_UnlockAudioDevice(a.device);
    if (old != NULL)
        openmpt_module_destroy(old);
    config_free(&cfg);
}

void audio_continue_music(const char *path)
{
    if (a.music == NULL || SDL_strcasecmp(a.music_cue, path) != 0)
        audio_play_music(path);
}

void audio_stop_music(void)
{
    openmpt_module *old;

    if (a.device == 0)
        return;
    SDL_LockAudioDevice(a.device);
    old = a.music;
    a.music = NULL;
    a.music_file[0] = '\0';
    a.music_cue[0] = '\0';
    SDL_UnlockAudioDevice(a.device);
    if (old != NULL)
        openmpt_module_destroy(old);
}
