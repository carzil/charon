#ifndef _STRING_H_
#define _STRING_H_

struct string {
    char* start;
    char* end;
};

#define string_size(s) ((s)->end - (s)->start)

/* Copies string of given size from raw buffer and puts a '\0' to the end. */
static inline char* copy_string_z(char* buf, size_t size) {
    char* result = (char*) malloc(size + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, buf, size);
    result[size] = '\0';
    return result;
}

static inline char* copy_string(char* buf, size_t size) {
    char* result = (char*) malloc(size);
    if (!result) {
        return NULL;
    }
    memcpy(result, buf, size);
    return result;
}

static inline int itoa(const char* buf) {
    int result = 0;
    while (*buf != '\0') {
        result *= 10;
        result += *buf - '0';
        buf++;
    }
    return result;
}

#endif