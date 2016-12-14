#ifndef _buffer_H_
#define _buffer_H_

#include "utils/list.h"
#include "utils/buffer.h"

struct chain_s {
    struct list buffers;
};

typedef struct chain_s chain_t;

chain_t* chain_create();
void chain_push_buffer(chain_t* c, struct buffer* buf);

#endif