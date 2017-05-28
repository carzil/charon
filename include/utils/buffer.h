#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils/list.h"
#include "utils/string.h"
#include "defs.h"

struct buffer {
    struct list_node node;

    unsigned in_file:1;
    unsigned in_memory:1;
    unsigned owning:1;

    char* start;
    char* end;
    char* last;

    int fd;
    size_t pos;
    size_t size;
};

typedef struct buffer buffer_t;

static inline int buffer_init(buffer_t* buffer)
{
    memset(buffer, '\0', sizeof(buffer_t));
    return CHARON_OK;
}

static inline buffer_t* buffer_create()
{
    buffer_t* buffer = (buffer_t*) malloc(sizeof(buffer_t));
    if (!buffer) {
        return NULL;
    }
    buffer_init(buffer);
    return buffer;
}

static inline void buffer_destroy(buffer_t* buffer)
{
    if (buffer->owning) {
        if (buffer->in_memory) {
            free(buffer->start);
        } else if (buffer->in_file) {
            close(buffer->fd);
        }
    }
}

#define buffer_size(b) ((size_t)((b)->end - (b)->start))
#define buffer_size_last(b) ((size_t)((b)->last - (b)->start))

static inline void buffer_malloc(buffer_t* buf, size_t size)
{
    buf->start = malloc(size);
    buf->in_memory = 1;
    buf->owning = 1;
    buf->end = buf->start + size;
    buf->last = buf->start;
}

static inline int buffer_realloc(buffer_t* buf, size_t new_size)
{
    char* new_ptr = realloc(buf->start, new_size);
    if (new_ptr == NULL) {
        return -CHARON_NO_MEM;
    }
    buf->start = new_ptr;
    buf->end = buf->start + new_size;
    return CHARON_OK;
}

static inline void buffer_clean(buffer_t* buf)
{
    memset(buf->start, '\0', buf->end - buf->start);
    buf->last = buf->start;
}

static inline void buffer_rewind(buffer_t* buf)
{
    memmove(buf->start, buf->start + buf->pos, buffer_size(buf) - buf->pos);
    buf->last -= buf->pos;
    buf->pos = 0;
}

static inline int buffer_string_copy(buffer_t* buf, string_t str)
{
    size_t sz = string_size(&str);
    if (buf->last + sz > buf->end) {
        return -CHARON_NO_MEM;
    }
    memcpy(buf->last, str.start, sz);
    return CHARON_OK;
}

static inline void buffer_update_size(buffer_t* buf)
{
    buf->size = buf->last - buf->start;
}

static inline void buffer_memcpy(buffer_t* dest, buffer_t* src)
{
    memcpy(dest->start, src->start, buffer_size_last(src));
}

static inline buffer_t* buffer_deep_copy(buffer_t* buf)
{
    buffer_t* copy = buffer_create();
    buffer_malloc(copy, buffer_size_last(buf));
    buffer_memcpy(copy, buf);
    copy->pos = 0;
    copy->last = copy->start + buffer_size_last(buf);
    copy->size = buffer_size_last(buf);
    return copy;
}

#define buffer_in_file(buf) do { (buf)->in_file = 1; (buf)->in_memory = 0; } while (0)
#define buffer_in_memory(buf) do { (buf)->in_file = 0; (buf)->in_memory = 1; } while (0)

#endif
