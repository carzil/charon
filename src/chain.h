#ifndef _buffer_H_
#define _buffer_H_

#include "utils/list.h"
#include "utils/buffer.h"

struct chain {
    struct list buffers;
};

struct chain* chain_create();
void chain_push_buffer(struct chain* c, struct buffer* buf);

#endif