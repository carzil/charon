#ifndef _STRING_H_
#define _STRING_H_

#include <string.h>
#include <stdlib.h>

struct string {
    char* start;
    char* end;
};

typedef struct string string_t;

#define STRING_EMPTY (string_t) { NULL, NULL }

#define string_size(s) ((size_t)((s)->end - (s)->start))

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
