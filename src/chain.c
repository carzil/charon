#include "utils/list.h"
#include "utils/logging.h"
#include "chain.h"
#include "errdefs.h"

int chain_init(struct chain* chain) {
    chain->buffers = LIST_EMPTY;
    return CHARON_OK;
}

struct chain* chain_create() {
    struct chain* chain = (struct chain*) malloc(sizeof(struct chain));
    if (!chain) {
        return NULL;
    }
    chain_init(chain);
    return chain;
}

void chain_push_buffer(struct chain* c, struct buffer* buf) {
    list_append(&c->buffers, &buf->node);
}
