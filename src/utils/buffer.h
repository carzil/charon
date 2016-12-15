#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stdint.h>
#include <string.h>

#include "utils/list.h"
#include "errdefs.h"

enum {
    BUFFER_IN_MEMORY = 1,
    BUFFER_IN_FILE = 1 << 1,
};

struct buffer {
    struct list_node node;

    int flags;

    size_t size;

    union {
        struct {
            void* start;
        } memory;

        struct {
            size_t pos;
            int fd;
        } file;
    };
};

static inline int buffer_init(struct buffer* buffer)
{
    buffer->node = LIST_NODE_EMPTY;
    buffer->size = 0;
    buffer->flags = 0;
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

#define buffer_size(b) ((b)->size)

#endif
