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
    list_node_t* ptr;
    list_node_t* tmp;

    list_foreach_safe(&chain->buffers, ptr, tmp) {
        buffer_t* buf = list_entry(ptr, buffer_t, node);
        buffer_destroy(buf);
        free(buf);
    }
}

void chain_push_buffer(chain_t* ch, buffer_t* buf)
{
    charon_debug("pushed buffer %p to chain %p", buf, ch);
    list_insert_last(&ch->buffers, &buf->node);
}

void chain_link_chain(chain_t* ch, chain_t* l)
{
    list_node_t* ptr;
    list_node_t* tmp;

    list_foreach_safe(&l->buffers, ptr, tmp) {
        list_insert_last(&ch->buffers, ptr);
    }
}

void chain_clear(chain_t* ch)
{
    list_node_t* ptr;
    list_node_t* tmp;

    list_foreach_safe(&ch->buffers, ptr, tmp) {
        buffer_t* buf = list_entry(ptr, buffer_t, node);
        if (buf->pos == buf->size) {
            list_remove(ptr);
            buffer_destroy(buf);
            free(buf);
        }
    }
}

void chain_clean(chain_t* ch)
{
    list_node_t* ptr;
    list_node_t* tmp;

    list_foreach_safe(&ch->buffers, ptr, tmp) {
        buffer_t* buf = list_entry(ptr, buffer_t, node);
        if (buf->pos == buf->size) {
            list_remove(ptr);
        }
    }
}

int chain_copy_and_push(chain_t* ch, buffer_t* buf)
{
    buffer_t* copy = buffer_deep_copy(buf);
    if (copy == NULL) {
        return -CHARON_NO_MEM;
    }
    buffer_rewind(buf);
    chain_push_buffer(ch, copy);

    return CHARON_OK;
}
