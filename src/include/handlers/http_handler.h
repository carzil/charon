#ifndef _HTTP_HANDLER_H_
#define _HTTP_HANDLER_H_

#include "client.h"
#include "server.h"


struct http_context_s {
    buffer_t req_buf;
    buffer_t body_buf;
    http_parser_t parser;
    http_request_t request;
    http_response_t response;
};

typedef struct http_context_s http_context_t;

#define http_context(ptr) ((http_context_t*) ptr)

void http_handler_connection_init(worker_t* w, connection_t* c);
void http_handler_on_request(worker_t* w, connection_t* c);
void http_handler_on_connection_end(worker_t* w, connection_t* c);
void http_handler_on_finish(worker_t* w);

#endif
