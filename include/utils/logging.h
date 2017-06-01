#ifndef _CHARON_LOGGING_
#define _CHARON_LOGGING_

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

typedef enum {
    DEBUG,
    INFO,
    ERROR,
    FATAL,
    PERROR
} loglevel_t;

static inline void charon_log(loglevel_t lvl, const char* fmt, va_list args)
{
    char buffer[4096];
    const char* lvl_string;
    size_t lvl_string_size;
    size_t offset;
    struct timeval tv;
    time_t nowtime;
    struct tm *nowtm;

    switch (lvl) {
    case DEBUG:
        lvl_string = "[debug]:";
        lvl_string_size = 8;
        break;
    case INFO:
        lvl_string = "[info]:";
        lvl_string_size = 7;
        break;
    case ERROR:
        lvl_string = "[err]:";
        lvl_string_size = 6;
        break;
    case FATAL:
        lvl_string = "[fatal]:";
        lvl_string_size = 8;
        break;
    case PERROR:
        lvl_string = "[err]:";
        lvl_string_size = 6;
        break;
    default:
        return;
    }

    gettimeofday(&tv, NULL);
    nowtime = tv.tv_sec;
    nowtm = localtime(&nowtime);
    offset = 0;

    offset += strftime(buffer + offset, sizeof(buffer) - offset, "%Y/%m/%d %H:%M:%S ", nowtm);
    // offset += snprintf(buffer, sizeof(buffer), "[%d] ", getpid());
    strcpy(buffer + offset, lvl_string);
    offset += lvl_string_size;
    buffer[offset++] = ' ';
    int count = vsnprintf(buffer + offset, 4096 - offset, fmt, args);

    if (count == -1) {
        /* TODO: handle error */
        return;
    }

    offset += count;
    if (lvl == PERROR) {
        char* error = strerror(errno);
        strcpy(buffer + offset, error);
        offset += strlen(error);
    }
    buffer[offset++] = '\n';
    write(2, buffer, offset);
}

#ifdef CHARON_DEBUG

__attribute__((format(__printf__, 1, 2))) static inline void charon_debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(DEBUG, fmt, args);
    va_end(args);
}

#else

#define charon_debug(...)

#endif

__attribute__((format(__printf__, 1, 2))) static inline void charon_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(INFO, fmt, args);
    va_end(args);
}

__attribute__((format(__printf__, 1, 2))) static inline void charon_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(ERROR, fmt, args);
    va_end(args);
}

__attribute__((format(__printf__, 1, 2))) static inline void charon_perror(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(PERROR, fmt, args);
    va_end(args);
}

__attribute__((format(__printf__, 1, 2))) static inline void charon_fatal(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(FATAL, fmt, args);
    va_end(args);
}


#endif
