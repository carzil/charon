#ifndef _buffer_H_
#define _buffer_H_

#include "utils/list.h"
#include "utils/buffer.h"

struct chain_s {
    LIST_HEAD_DECLARE(buffers);
};

typedef struct chain_s chain_t;

int chain_init(chain_t* ch);
chain_t* chain_create();
void chain_push_buffer(chain_t* ch, buffer_t* buf);
void chain_destroy(chain_t* ch);
void chain_clear(chain_t* ch);
void chain_clean(chain_t* ch);

#endif
