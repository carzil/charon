#ifndef _buffer_H_
#define _buffer_H_

#include "utils/list.h"
#include "utils/buffer.h"

struct chain_s {
    LIST_HEAD_DECLARE(buffers);
};

typedef struct chain_s chain_t;

int chain_init(chain_t* chain);
chain_t* chain_create();
void chain_push_buffer(chain_t* c, buffer_t* buf);
void chain_destroy(chain_t* c);

#endif
