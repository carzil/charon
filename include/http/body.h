#ifndef _CHARON_HTTP_BODY_H_
#define _CHARON_HTTP_BODY_H_

#include "http.h"
#include "connection.h"

struct http_body_s {
    chain_t* chain;
    buffer_t* buf;

    size_t buffer_size;
    size_t body_size;
    size_t read;
};

typedef struct http_body_s http_body_t;

int http_body_init(http_body_t* body, chain_t* chain, size_t body_size, size_t buffer_size);
int http_body_preread(http_body_t* body, buffer_t* buf, size_t sz);
int http_body_read(connection_t* c, http_body_t* body);
void http_body_cleanup(http_body_t* body);

#define http_body_done(body) ((body)->read == (body)->body_size)

#endif
