#include "utils/list.h"
#include "utils/logging.h"
#include "chain.h"
#include "defs.h"

int chain_init(chain_t* chain)
{
    list_head_init(chain->buffers);
    return CHARON_OK;
}

chain_t* chain_create()
{
    chain_t* chain = (chain_t*) malloc(sizeof(chain_t));
    if (!chain) {
        return NULL;
    }
    chain_init(chain);
    return chain;
}

void chain_destroy(UNUSED chain_t* chain)
{

}

void chain_push_buffer(chain_t* ch, buffer_t* buf)
{
    list_insert_last(&ch->buffers, &buf->node);
}
