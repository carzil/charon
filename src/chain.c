#include "utils/list.h"
#include "utils/logging.h"
#include "chain.h"
#include "errdefs.h"

int chain_init(chain_t* chain)
{
    chain->buffers = LIST_EMPTY;
    return CHARON_OK;
}

chain_t* chrn_chain_create()
{
    chain_t* chain = (chain_t*) malloc(sizeof(chain_t));
    if (!chain) {
        return NULL;
    }
    chain_init(chain);
    return chain;
}

void chrn_chain_push_buffer(chain_t* ch, struct buffer* buf)
{
    list_append(&ch->buffers, &buf->node);
}
