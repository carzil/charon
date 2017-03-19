#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils/list.h"
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

static inline int buffer_init(struct buffer* buffer)
{
    memset(buffer, '\0', sizeof(struct buffer));
    return CHARON_OK;
}

static inline struct buffer* buffer_create()
{
    struct buffer* buffer = (struct buffer*) malloc(sizeof(struct buffer));
    if (!buffer) {
        return NULL;
    }
    buffer_init(buffer);
    return buffer;
}

static inline void buffer_destroy(struct buffer* buffer)
{
    if (buffer->owning) {
        if (buffer->in_memory) {
            free(buffer->start);
        } else if (buffer->in_file) {
            close(buffer->fd);
        }
    }
}

#define buffer_size(b) ((b)->end - (b)->start)

static inline void buffer_malloc(struct buffer* buf, size_t size)
{
    buf->start = malloc(size);
    memset(buf->start, '\0', size);
    buf->in_memory = 1;
    buf->owning = 1;
    buf->end = buf->start + size;
    buf->last = buf->start;
}

static inline void buffer_clean(struct buffer* buf)
{
    memset(buf->start, '\0', buf->end - buf->start);
    buf->last = buf->start;
}

#define buffer_in_file(buf) do { (buf)->in_file = 1; (buf)->in_memory = 0; } while (0)
#define buffer_in_memory(buf) do { (buf)->in_file = 0; (buf)->in_memory = 1; } while (0)

typedef struct buffer buffer_t;

#endif
