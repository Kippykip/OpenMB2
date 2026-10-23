/*
 * log.h - tiny logging helpers.
 *
 * Everything goes to stderr with a level prefix. The core library does not
 * depend on SDL, so the command-line tools can use it too.
 */
#ifndef OPENMB2_LOG_H
#define OPENMB2_LOG_H

#if defined(__GNUC__) || defined(__clang__)
#define LOG_PRINTF(fmt_index) __attribute__((format(printf, fmt_index, fmt_index + 1)))
#else
#define LOG_PRINTF(fmt_index)
#endif

typedef enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

/* Messages below this level are dropped. Defaults to LOG_INFO. */
void log_set_level(LogLevel level);

void log_message(LogLevel level, const char *fmt, ...) LOG_PRINTF(2);

#define log_debug(...) log_message(LOG_DEBUG, __VA_ARGS__)
#define log_info(...)  log_message(LOG_INFO, __VA_ARGS__)
#define log_warn(...)  log_message(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_message(LOG_ERROR, __VA_ARGS__)

#endif
