/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "crash.h"

#include <stdio.h>
#include <string.h>

#ifndef OPENMB2_VERSION
#define OPENMB2_VERSION "?"
#endif

#define MAX_FRAMES 48

/* Made when installing: a crash handler shouldn't count on much. */
static char g_path[1024];
static const char *const k_build = "OpenMB2 " OPENMB2_VERSION " (built " __DATE__ " " __TIME__ ")";

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* "openmb2.exe+0x1234" for an address in a loaded module. */
static void describe(void *address, char *out, size_t size)
{
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    const char *base;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)address, &module) &&
        GetModuleFileNameA(module, name, sizeof(name)) > 0) {
        base = strrchr(name, '\\');
        snprintf(out, size, "%s+0x%llx", base != NULL ? base + 1 : name,
                 (unsigned long long)((const char *)address - (const char *)module));
    } else {
        snprintf(out, size, "0x%llx", (unsigned long long)(size_t)address);
    }
}

static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    default: return "exception";
    }
}

static void put(FILE *file, const char *text)
{
    fputs(text, stderr);
    if (file != NULL)
        fputs(text, file);
}

/* Walks the crashed thread's stack from the exception's own registers. */
static LONG WINAPI on_crash(EXCEPTION_POINTERS *info)
{
    FILE *file = g_path[0] != '\0' ? fopen(g_path, "w") : NULL;
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    char line[512], where[300];
    int i;

    put(file, "\n");
    put(file, k_build);
    describe(record->ExceptionAddress, where, sizeof(where));
    snprintf(line, sizeof(line), "\ncrashed: %s (0x%08lx) at %s\n", exception_name(record->ExceptionCode),
             (unsigned long)record->ExceptionCode, where);
    put(file, line);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        snprintf(line, sizeof(line), "%s address 0x%llx\n",
                 record->ExceptionInformation[0] == 0 ? "reading" : "writing",
                 (unsigned long long)record->ExceptionInformation[1]);
        put(file, line);
    }
    put(file, "stack:\n");
#if defined(_M_X64) || defined(__x86_64__)
    {
        CONTEXT context = *info->ContextRecord;
        for (i = 0; i < MAX_FRAMES && context.Rip != 0; i++) {
            DWORD64 image = 0;
            PRUNTIME_FUNCTION function;
            PVOID handler_data;
            DWORD64 frame;

            describe((void *)(size_t)context.Rip, where, sizeof(where));
            snprintf(line, sizeof(line), "  %2d %s\n", i, where);
            put(file, line);
            function = RtlLookupFunctionEntry(context.Rip, &image, NULL);
            if (function == NULL) {
                /* A leaf function: the return address is on top. */
                context.Rip = *(DWORD64 *)(size_t)context.Rsp;
                context.Rsp += 8;
            } else {
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler_data,
                                 &frame, NULL);
            }
        }
    }
#else
    {
        void *frames[MAX_FRAMES];
        USHORT count = RtlCaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);
        for (i = 0; i < count; i++) {
            describe(frames[i], where, sizeof(where));
            snprintf(line, sizeof(line), "  %2d %s\n", i, where);
            put(file, line);
        }
    }
#endif
    if (file != NULL)
        fclose(file);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void crash_install(const char *path)
{
    ULONG reserve = 64 * 1024;

    snprintf(g_path, sizeof(g_path), "%s", path != NULL ? path : "");
    /* Room for the handler to run after a stack overflow. */
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(on_crash);
}

#elif defined(__linux__) && !defined(__ANDROID__)

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static char g_stack[64 * 1024];

static void put(int fd, const char *text)
{
    ssize_t ignored = write(STDERR_FILENO, text, strlen(text));
    if (fd >= 0)
        ignored = write(fd, text, strlen(text));
    (void)ignored;
}

static void on_crash(int sig, siginfo_t *info, void *context)
{
    int fd = g_path[0] != '\0' ? open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
    void *frames[MAX_FRAMES];
    char line[200];
    int count;

    (void)context;
    put(fd, "\n");
    put(fd, k_build);
    snprintf(line, sizeof(line), "\ncrashed: %s (signal %d), address %p\nstack:\n", strsignal(sig), sig,
             info->si_addr);
    put(fd, line);
    count = backtrace(frames, MAX_FRAMES);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    if (fd >= 0) {
        backtrace_symbols_fd(frames, count, fd);
        close(fd);
    }
    /* Let it end the way it would have. */
    signal(sig, SIG_DFL);
    raise(sig);
}

void crash_install(const char *path)
{
    static const int signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    stack_t stack;
    struct sigaction action;
    size_t i;
    void *warm[1];

    snprintf(g_path, sizeof(g_path), "%s", path != NULL ? path : "");
    /* backtrace() loads libgcc the first time; not inside the handler. */
    backtrace(warm, 1);
    /* Its own stack, so a stack overflow can still be reported. */
    stack.ss_sp = g_stack;
    stack.ss_size = sizeof(g_stack);
    stack.ss_flags = 0;
    sigaltstack(&stack, NULL);
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = on_crash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
        sigaction(signals[i], &action, NULL);
}

#else

void crash_install(const char *path)
{
    (void)path;
}

#endif
