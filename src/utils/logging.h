#ifndef _CHARON_LOGGING_
#define _CHARON_LOGGING_

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

typedef enum {
    DEBUG,
    INFO,
    ERROR,
    PERROR
} loglevel_t;

static inline void charon_log(loglevel_t lvl, const char* fmt, va_list args)
{
    char buffer[4096];

    const char* lvl_string;

    switch (lvl) {
        case DEBUG:
            lvl_string = "[DEBUG] ";
            break;
        case INFO:
            lvl_string = "[INFO] ";
            break;
        case ERROR:
            lvl_string = "[ERROR] ";
            break;
        case PERROR:
            lvl_string = "[ERROR] ";
            break;
    }

    char new_fmt[4096];

    memcpy(new_fmt, lvl_string, strlen(lvl_string));
    size_t fmt_size = strlen(lvl_string);
    memcpy(new_fmt + fmt_size, fmt, strlen(fmt));
    fmt_size += strlen(fmt);
    if (lvl == PERROR) {
        char* error = strerror(errno);
        size_t error_len = strlen(error);
        memcpy(new_fmt + fmt_size, error, error_len);
        fmt_size += error_len;
    }
    memcpy(new_fmt + fmt_size, "\n", 1);
    new_fmt[fmt_size + 1] = '\0';
    fmt = (const char*)&new_fmt;


    size_t len = vsnprintf(buffer, 4096, fmt, args);

    write(2, buffer, len);
}

#ifdef CHARON_DEBUG

static inline void charon_debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(DEBUG, fmt, args);
    va_end(args);
}

#else

#define charon_debug(fmt, ...)

#endif

static inline void charon_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(INFO, fmt, args);
    va_end(args);
}

static inline void charon_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(ERROR, fmt, args);
    va_end(args);
}

static inline void charon_perror(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    charon_log(PERROR, fmt, args);
    va_end(args);
}


#endif
