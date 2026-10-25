/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "online.h"
#include "core/des3.h"
#include "core/log.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#endif

#define BODY_SIZE  0x20000 /* the original's request and reply buffer */
#define MAX_ROWS   600

/* The key the original encrypts every request with (0x47fa6c). */
static const char k_key[] = "tsbstrkndnthwrld";
/* What the original says it is; Smash Frenzy 2 says "SF2.4". */
static const char k_agent[] = "Magic Ball 2 ver .2";
static const char k_regto[] = "Full version";

static const char *const k_packs[] = { "Levels", "Levels1", "Levels2", "Levels3", "Levels4",
                                       "Levels5", "Levels6", "Levels7", "Levels0r" };
static const char *const k_bats[] = { "Pads/common.txt", "Pads/onefan.txt", "Pads/nofan.txt" };

static struct {
    bool enabled;
    bool no_sends;        /* cheats are on: tables only */
    char server[256];
    char build[64];
    char hid[300];
    char gpu[256];        /* v: the graphics card */
    char cpu[260];        /* c: the processor */
    int mhz;              /* f */
    SDL_mutex *lock;
    SDL_Thread *thread;
    SDL_atomic_t activated;
    SDL_atomic_t state;
    OnlineRow *rows;      /* written by the thread while loading */
    int row_count;
} on;

/* ------------------------------------------------------------------------ */
/* Who is asking                                                            */
/* ------------------------------------------------------------------------ */

static int byte_sum(const char *s)
{
    int sum = 0;
    for (; *s != '\0'; s++)
        sum += (unsigned char)*s;
    return sum;
}

#ifdef _WIN32
/* 0x414c50: the C: drive's label and serial number, "<label sum>s<serial>",
   or "<folder>e<error>" if Windows won't say. */
static void machine_id(const char *fallback)
{
    char label[MAX_PATH + 1] = "", system[MAX_PATH + 1];
    DWORD serial = 0, length, flags;

    (void)fallback;
    if (GetVolumeInformationA("C:\\", label, sizeof(label), &serial, &length, &flags, system,
                              sizeof(system))) {
        snprintf(on.hid, sizeof(on.hid), "%ds%X", byte_sum(label), (unsigned)serial);
    } else {
        char folder[MAX_PATH] = "";
        DWORD error = GetLastError();
        GetCurrentDirectoryA(sizeof(folder), folder);
        snprintf(on.hid, sizeof(on.hid), "%se%d", folder, (int)error);
    }
}

/* 0x414b90: the processor's name and speed from the registry. */
static void processor(void)
{
    HKEY key;
    DWORD size = sizeof(on.cpu), mhz = 0, mhz_size = sizeof(mhz);

    snprintf(on.cpu, sizeof(on.cpu), "Unknown");
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExA(key, "ProcessorNameString", NULL, NULL, (BYTE *)on.cpu, &size) != ERROR_SUCCESS)
        snprintf(on.cpu, sizeof(on.cpu), "Unknown");
    on.cpu[sizeof(on.cpu) - 1] = '\0';
    if (RegQueryValueExA(key, "~MHz", NULL, NULL, (BYTE *)&mhz, &mhz_size) == ERROR_SUCCESS)
        on.mhz = (int)mhz;
    RegCloseKey(key);
}
#else
static void machine_id(const char *fallback)
{
    snprintf(on.hid, sizeof(on.hid), "%s", fallback);
}

static void processor(void)
{
    snprintf(on.cpu, sizeof(on.cpu), "Unknown");
}
#endif

/* ------------------------------------------------------------------------ */
/* Talking to the server                                                    */
/* ------------------------------------------------------------------------ */

#ifdef _WIN32
/* 0x414e00, with the host and path from the server setting. */
static bool http_post(const char *path, const unsigned char *body, size_t size, char *reply, size_t reply_size)
{
    URL_COMPONENTSA url;
    char host[256], object[512];
    HINTERNET net, connection, request;
    DWORD read = 0, flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    size_t got = 0;
    bool ok = false;

    memset(&url, 0, sizeof(url));
    url.dwStructSize = sizeof(url);
    url.lpszHostName = host;
    url.dwHostNameLength = sizeof(host);
    url.lpszUrlPath = object;
    url.dwUrlPathLength = sizeof(object);
    if (!InternetCrackUrlA(on.server, 0, 0, &url))
        return false;
    snprintf(object + strlen(object), sizeof(object) - strlen(object), "%s", path);
    if (url.nScheme == INTERNET_SCHEME_HTTPS)
        flags |= INTERNET_FLAG_SECURE;

    net = InternetOpenA(k_agent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (net == NULL)
        return false;
    {
        /* The original waits for ever; a dead server shouldn't hold a thread that long. */
        DWORD timeout = 20000;
        InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(net, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    }
    connection = InternetConnectA(net, host, url.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    request = connection != NULL ? HttpOpenRequestA(connection, "POST", object, NULL, NULL, NULL, flags, 0) : NULL;
    if (request != NULL && HttpSendRequestA(request, NULL, 0, (LPVOID)body, (DWORD)size)) {
        while (got + 1 < reply_size &&
               InternetReadFile(request, reply + got, (DWORD)(reply_size - 1 - got), &read) && read > 0)
            got += read;
        ok = true;
    }
    reply[got] = '\0';
    if (request != NULL)
        InternetCloseHandle(request);
    if (connection != NULL)
        InternetCloseHandle(connection);
    InternetCloseHandle(net);
    return ok;
}
#elif defined(__EMSCRIPTEN__)
static bool http_post(const char *path, const unsigned char *body, size_t size, char *reply, size_t reply_size)
{
    (void)path, (void)body, (void)size, (void)reply_size;
    reply[0] = '\0';
    return false;
}
#else
/* Elsewhere the curl program does the talking: it is on nearly every
   system and knows https. The body goes through a file. */
static bool http_post(const char *path, const unsigned char *body, size_t size, char *reply, size_t reply_size)
{
    char file[] = "/tmp/openmb2-XXXXXX", command[1024];
    FILE *out, *in;
    size_t got = 0, n;
    int fd = mkstemp(file);
    bool ok;

    reply[0] = '\0';
    if (fd < 0)
        return false;
    out = fdopen(fd, "wb");
    if (out == NULL || fwrite(body, 1, size, out) != size) {
        if (out != NULL)
            fclose(out);
        remove(file);
        return false;
    }
    fclose(out);
    /* The server comes from the settings; keep quotes out of the command. */
    if (strchr(on.server, '\'') != NULL || strchr(path, '\'') != NULL) {
        remove(file);
        return false;
    }
    snprintf(command, sizeof(command),
             "curl -s -m 20 -A '%s' -H 'Content-Type:' --data-binary @'%s' '%s%s'", k_agent, file,
             on.server, path);
    in = popen(command, "r");
    if (in == NULL) {
        remove(file);
        return false;
    }
    while (got + 1 < reply_size && (n = fread(reply + got, 1, reply_size - 1 - got, in)) > 0)
        got += n;
    reply[got] = '\0';
    ok = pclose(in) == 0;
    remove(file);
    return ok;
}
#endif

/* 0x414d00: encrypt the line, padded with zeros to the next 128 bytes,
   and send it. */
static bool request(const char *path, const char *line, char *reply, size_t reply_size)
{
    size_t length = strlen(line), size = (length / 128 + 1) * 128;
    unsigned char *body = calloc(1, size);
    bool ok;

    if (body == NULL)
        return false;
    memcpy(body, line, length);
    des3_ecb_encrypt((const unsigned char *)k_key, strlen(k_key), body, size);
    ok = http_post(path, body, size, reply, reply_size);
    free(body);
    return ok;
}

/* 0x414f30: once per run, until the server says "OK". */
static void activate(char *buffer)
{
    char line[2048];

    if (SDL_AtomicGet(&on.activated))
        return;
    snprintf(line, sizeof(line), "v=%s&vcs=%d&c=%s&ccs=%d&f=%d&hID=%s&regto=%s&build=%s&e=e", on.gpu,
             byte_sum(on.gpu), on.cpu, byte_sum(on.cpu), on.mhz, on.hid, k_regto, on.build);
    if (request("/scripts/mb2activation.php", line, buffer, 0x1fa0) && strcmp(buffer, "OK") == 0)
        SDL_AtomicSet(&on.activated, 1);
}

/* ------------------------------------------------------------------------ */
/* Reading the table                                                        */
/* ------------------------------------------------------------------------ */

/* An attribute's value, with XML's five escapes undone; false if absent. */
static bool attribute(const char *tag, const char *end, const char *name, char *out, size_t size)
{
    size_t length = strlen(name);
    const char *p = tag;

    while ((p = strstr(p, name)) != NULL && p < end) {
        const char *q = p + length;
        char quote;
        size_t n = 0;

        if ((p > tag && !isspace((unsigned char)p[-1])) || (*q != '=' && !isspace((unsigned char)*q))) {
            p = q;
            continue;
        }
        while (isspace((unsigned char)*q))
            q++;
        if (*q++ != '=')
            continue;
        while (isspace((unsigned char)*q))
            q++;
        quote = *q++;
        if (quote != '"' && quote != '\'')
            continue;
        for (; q < end && *q != quote; q++) {
            static const struct { const char *name; char c; } k_escapes[] = {
                { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }
            };
            char c = *q;
            size_t i;
            for (i = 0; i < sizeof(k_escapes) / sizeof(k_escapes[0]); i++) {
                size_t l = strlen(k_escapes[i].name);
                if (strncmp(q, k_escapes[i].name, l) == 0) {
                    c = k_escapes[i].c;
                    q += l - 1;
                    break;
                }
            }
            if (n + 1 < size)
                out[n++] = c;
        }
        if (size > 0)
            out[n] = '\0';
        return true;
    }
    if (size > 0)
        out[0] = '\0';
    return false;
}

/* 0x41729c: the reply is a list of <rec/>, <mes/> and <res/> lines. */
static int parse_rows(const char *xml, OnlineRow *rows, int max)
{
    const char *p = xml;
    int count = 0;

    while (count < max && (p = strchr(p, '<')) != NULL) {
        const char *end = strchr(p, '>');
        OnlineRow *row = &rows[count];
        char value[64];

        if (end == NULL)
            break;
        memset(row, 0, sizeof(*row));
        if (strncmp(p + 1, "rec", 3) == 0 && !isalnum((unsigned char)p[4])) {
            attribute(p, end, "n", row->name, sizeof(row->name));
            row->score = attribute(p, end, "s", value, sizeof(value)) ? atoi(value) : 0;
            row->bat = attribute(p, end, "b", value, sizeof(value)) ? atoi(value) : 0;
            attribute(p, end, "d", row->date, sizeof(row->date));
            attribute(p, end, "t", row->time, sizeof(row->time));
            attribute(p, end, "l", row->level, sizeof(row->level));
            row->flagged = attribute(p, end, "a", value, sizeof(value));
            count++;
        } else if (strncmp(p + 1, "mes", 3) == 0 && !isalnum((unsigned char)p[4])) {
            row->message = true;
            attribute(p, end, "m", row->text, sizeof(row->text));
            row->color = attribute(p, end, "c", value, sizeof(value)) ? (uint32_t)strtoul(value, NULL, 16)
                                                                      : 0xffffff00u;
            row->scale = attribute(p, end, "z", value, sizeof(value)) ? (float)atof(value) : 0.75f;
            row->flagged = attribute(p, end, "a", value, sizeof(value));
            attribute(p, end, "i", row->url, sizeof(row->url));
            count++;
        }
        p = end + 1;
    }
    return count;
}

/* ------------------------------------------------------------------------ */
/* The worker                                                               */
/* ------------------------------------------------------------------------ */

typedef struct Job {
    bool send;
    char line[1024];
} Job;

static int run_job(void *data)
{
    Job *job = data;
    char *buffer = malloc(BODY_SIZE);

    if (buffer != NULL) {
        activate(buffer);
        if (job->send) {
            /* 0x417ad0: the answer doesn't matter. */
            request("/scripts/mb2addscore.php", job->line, buffer, BODY_SIZE);
        } else if (request("/scripts/mb2getscore.php", job->line, buffer, BODY_SIZE)) {
            int count = parse_rows(buffer, on.rows, MAX_ROWS);
            SDL_LockMutex(on.lock);
            on.row_count = count;
            SDL_UnlockMutex(on.lock);
            SDL_AtomicSet(&on.state, ONLINE_READY);
            log_info("online: %d lines in the table", count);
        } else {
            SDL_AtomicSet(&on.state, ONLINE_FAILED);
            log_warn("online: no answer from %s", on.server);
        }
    }
    free(buffer);
    free(job);
    return 0;
}

/* One request at a time: a new one waits for the last to end. */
static void start_job(Job *job)
{
    if (on.thread != NULL) {
        SDL_WaitThread(on.thread, NULL);
        on.thread = NULL;
    }
    on.thread = SDL_CreateThread(run_job, "online", job);
    if (on.thread == NULL) {
        if (!job->send)
            SDL_AtomicSet(&on.state, ONLINE_FAILED);
        free(job);
    }
}

/* ------------------------------------------------------------------------ */

void online_init(const char *server, const char *gpu, const char *fallback_id)
{
    size_t length;

    online_shutdown();
    on.lock = SDL_CreateMutex();
    on.rows = calloc(MAX_ROWS, sizeof(*on.rows));
    snprintf(on.server, sizeof(on.server), "%s", server != NULL ? server : "");
    length = strlen(on.server);
    while (length > 0 && on.server[length - 1] == '/')
        on.server[--length] = '\0';
    snprintf(on.gpu, sizeof(on.gpu), "%s", gpu != NULL ? gpu : "");
    snprintf(on.build, sizeof(on.build), "Default");
    machine_id(fallback_id != NULL ? fallback_id : "");
    processor();
    on.enabled = on.server[0] != '\0' && on.lock != NULL && on.rows != NULL;
    log_info("online: %s", on.enabled ? on.server : "off");
}

void online_shutdown(void)
{
    if (on.thread != NULL)
        SDL_WaitThread(on.thread, NULL);
    if (on.lock != NULL)
        SDL_DestroyMutex(on.lock);
    free(on.rows);
    memset(&on, 0, sizeof(on));
}

void online_set_build(const char *build)
{
    if (build != NULL && build[0] != '\0')
        snprintf(on.build, sizeof(on.build), "%s", build);
}

/* 0x415570 */
void online_fetch(int pack)
{
    Job *job;

    if (!on.enabled) {
        SDL_AtomicSet(&on.state, ONLINE_FAILED);
        return;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL)
        return;
    if (on.thread != NULL) {
        SDL_WaitThread(on.thread, NULL);
        on.thread = NULL;
    }
    on.row_count = 0;
    SDL_AtomicSet(&on.state, ONLINE_LOADING);
    snprintf(job->line, sizeof(job->line), "hID=%s&pack=%d&regto=%s&build=%s&e=e", on.hid, pack < 0 ? 0 : pack,
             k_regto, on.build);
    start_job(job);
}

OnlineState online_state(void)
{
    return (OnlineState)SDL_AtomicGet(&on.state);
}

int online_rows(const OnlineRow **rows)
{
    *rows = on.rows;
    return online_state() == ONLINE_READY ? on.row_count : 0;
}

/* 0x417c10 */
void online_block_sends(bool block)
{
    on.no_sends = block;
}

void online_send(const char *name, int score, int level_score, int level, int pack, int bat)
{
    Job *job;

    if (!on.enabled || score < 1000000 || pack < 0)
        return;
    if (on.no_sends) {
        log_info("online: not sending %d, cheats are on", score);
        return;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL)
        return;
    job->send = true;
    snprintf(job->line, sizeof(job->line),
             "name=%s&score=%d&scoreA=%d&level=%d&pack=%d&bat=%d&hID=%s&regto=%s&build=%s&vcs=%d&ccs=%d&f=%d&e=e",
             name[0] != '\0' ? name : "Unnamed", score, level_score, level, pack, bat, on.hid, k_regto, on.build,
             byte_sum(on.gpu), byte_sum(on.cpu), on.mhz);
    log_info("online: sending %d for %s", score, name);
    start_job(job);
}

int online_pack_number(const char *folder)
{
    size_t i;

    for (i = 0; folder != NULL && i < sizeof(k_packs) / sizeof(k_packs[0]); i++)
        if (SDL_strcasecmp(folder, k_packs[i]) == 0)
            return (int)i;
    return -1;
}

int online_bat_number(const char *pad)
{
    size_t i;

    for (i = 0; pad != NULL && i < sizeof(k_bats) / sizeof(k_bats[0]); i++)
        if (SDL_strcasecmp(pad, k_bats[i]) == 0)
            return (int)i;
    return 0;
}
