#include "http/body.h"

int http_body_alloc_buffer(http_body_t* body)
{
    if (body->buf != NULL && body->buf->last == body->buf->end) {
        buffer_update_size(body->buf);
        chain_push_buffer(body->chain, body->buf);
        body->buf = NULL;
    }

    if (body->buf == NULL) {
        body->buf = buffer_create();
        if (body->buf == NULL) {
            return -CHARON_NO_MEM;
        }
        buffer_malloc(body->buf, body->buffer_size);
        if (body->buf->start == NULL) {
            return -CHARON_NO_MEM;
        }
    }

    return CHARON_OK;
}

int http_body_init(http_body_t* body, chain_t* chain, size_t body_size, size_t buffer_size)
{
    body->read = 0;
    body->body_size = body_size;
    body->buffer_size = buffer_size;
    body->buf = NULL;
    body->chain = chain;
    return CHARON_OK;
}

void http_body_cleanup(http_body_t* body)
{
    body->read = 0;
    body->body_size = 0;

    /* TODO: possible memory leak */
    body->buf = NULL;
}

int http_body_preread(http_body_t* body, buffer_t* buf, size_t sz)
{
    int res;

    if (body->body_size < sz) {
        sz = body->body_size;
    }

    charon_debug("body preread=%zu", sz);

    /* TODO: buffer overflow! */
    if ((res = http_body_alloc_buffer(body)) != CHARON_OK) {
        return res;
    }
    memcpy(body->buf->start, buf->start + buf->pos, sz);
    body->read = sz;
    body->buf->last += sz;

    if (sz == body->body_size) {
        buffer_update_size(body->buf);
        chain_push_buffer(body->chain, body->buf);
        body->buf = NULL;
    }
    return sz;
}

int http_body_read(connection_t* c, http_body_t* body)
{
    ssize_t count;

    for (;;) {
        http_body_alloc_buffer(body);
        count = conn_read_max(c, body->buf, body->body_size - body->read);
        if (count != -CHARON_BUFFER_FULL) {
            break;
        }
        body->read += buffer_size(body->buf);
    };

    if (count < 0) {
        return count;
    }

    body->read += count;

    if (body->read == body->body_size) {
        buffer_update_size(body->buf);
        if (body->buf->size > 0) {
            chain_push_buffer(body->chain, body->buf);
        }
        body->buf = NULL;
        return CHARON_OK;
    }

    if (c->eof) {
        return CHARON_OK;
    }


    return CHARON_AGAIN;
}
