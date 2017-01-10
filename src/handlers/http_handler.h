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

void http_handler_connection_init(connection_t* c);
void http_handler_on_request(charon_server* s, connection_t* c);
void http_handler_on_request_body(charon_server* s, connection_t* c);
void http_handler_on_connection_end(charon_server* s, connection_t* c);
void http_handler_on_finish(charon_server* s);
void http_handler_process_request(charon_server* s, connection_t* c);
void http_handler_make_response(charon_server* s, connection_t* c);

#endif
