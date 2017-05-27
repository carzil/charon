#ifndef _HTTP_HANDLER_H_
#define _HTTP_HANDLER_H_

#include "connection.h"
#include "utils/list.h"
#include "utils/vector.h"
#include "http/parser.h"
#include "http/vhost.h"
#include "http/upstream.h"

struct http_context_s {
    buffer_t req_buf;
    http_parser_t parser;
    http_request_t request;
    http_response_t response;

    http_header_t header;

    http_upstream_connection_t* upstream_conn;
};

struct http_handler_s {
    handler_t handler;

    LIST_HEAD_DECLARE(vhosts);
};

typedef struct http_context_s http_context_t;
typedef struct http_handler_s http_handler_t;

#define http_context(ptr) ((http_context_t*) ptr)

http_handler_t* http_handler_on_init();
void http_handler_on_finish(http_handler_t* h);

int http_handler_connection_init(connection_t* c);
void http_handler_on_connection_end(connection_t* c);
int http_end_process_request(connection_t* c, http_status_t error);
void http_handler_cleanup_connection(connection_t* c);

typedef struct {
    time_t accept_timeout;
    size_t client_buffer_size;
} http_main_conf_t;

typedef struct {
    http_main_conf_t main;
    VECTOR_DEFINE(vhosts, vhost_t);
} http_conf_t;

#endif
