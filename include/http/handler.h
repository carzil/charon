#ifndef _HTTP_HANDLER_H_
#define _HTTP_HANDLER_H_

#include "connection.h"
#include "utils/list.h"
#include "utils/vector.h"
#include "http/parser.h"
#include "http/vhost.h"
#include "http/upstream.h"

struct http_connection_s {
    connection_t conn;

    buffer_t req_buf;
    http_parser_t parser;
    http_request_t request;
    http_response_t response;

    http_header_t header;
    http_body_t body;
    chain_t body_chain;

    http_upstream_connection_t* upstream_conn;

    unsigned force_close:1;
};

typedef struct http_connection_s http_connection_t;
#define http_connection(ptr) ((http_connection_t*) ptr)

struct http_handler_s {
    handler_t handler;

    LIST_HEAD_DECLARE(vhosts);
};

typedef struct http_handler_s http_handler_t;


http_handler_t* http_handler_create();
void http_handler_destroy(http_handler_t* h);

void http_handler_on_connection_end(connection_t* c);
int http_end_process_request(http_connection_t* c, http_status_t error);
void http_handler_cleanup_connection(http_connection_t* c);
int http_prepare_for_next_request(http_connection_t* hc);

typedef struct {
    time_t accept_timeout;
    size_t client_buffer_size;
} http_main_conf_t;

typedef struct {
    http_main_conf_t main;
    VECTOR_DEFINE(vhosts, vhost_t);
} http_conf_t;

#endif
