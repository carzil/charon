#ifndef _STRING_H_
#define _STRING_H_

#include <string.h>
#include <stdlib.h>

#include "utils/logging.h"

struct string {
    char* start;
    char* end;
};

typedef struct string string_t;

#define STRING_EMPTY (string_t) { NULL, NULL }

#define string_size(s) ((size_t)((s)->end - (s)->start))
#define string(s) ((string_t) { s, s + sizeof(s) - 1 } )

static inline void string_clone(string_t* to, string_t* from)
{
    to->start = from->start;
    to->end = to->end;
}

static inline int string_cmp(string_t* a, string_t* b)
{
    size_t sz = string_size(a);
    if (string_size(b) < sz) {
        sz = string_size(b);
    }
    // charon_debug("cmp('%.*s', '%.*s')", (int)string_size(a), a->start, (int)string_size(b), b->start);
    return strncmp(a->start, b->start, sz);
}

static inline int string_cmpl(string_t* a, char* b)
{
    return strncmp(a->start, b, string_size(a));
}

/* Copies string of given size from raw buffer and puts a '\0' to the end. */
static inline char* copy_string_z(char* buf, size_t size)
{
    char* result = (char*) malloc(size + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, buf, size);
    result[size] = '\0';
    return result;
}

static inline char* copy_string(char* buf, size_t size)
{
    char* result = (char*) malloc(size);
    if (!result) {
        return NULL;
    }
    memcpy(result, buf, size);
    return result;
}

static inline int string_to_int(const string_t* str)
{
    int result = 0;
    char* pos = str->start;
    while (pos != str->end) {
        result *= 10;
        result += *pos - '0';
        pos++;
    }
    return result;
}


#endif
