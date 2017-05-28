#ifndef _CHARON_HTTP_UPSTREAM_
#define _CHARON_HTTP_UPSTREAM_

#include "connection.h"
#include "utils/list.h"
#include "http/parser.h"

struct http_connection_s;

typedef struct {
    string_t uri;

    LIST_HEAD_DECLARE(idle_connections);
    LIST_HEAD_DECLARE(connections);
} http_upstream_t;


typedef struct {
    connection_t conn;

    struct http_connection_s* upstreaming;
    http_upstream_t* upstream;
    chain_t chain_in;
    chain_t chain_out;

    buffer_t recv_buf;

    http_parser_t parser;
    http_response_t resp;

    http_header_t header;

    size_t body_read;

    unsigned status_line_parsed:1;
    unsigned headers_parsed:1;
    unsigned response_received:1;
    unsigned discard_response:1;
} http_upstream_connection_t;

http_upstream_connection_t* http_upstream_connect(http_upstream_t* upstream);
int http_upstream_bond(http_upstream_connection_t* uc, struct http_connection_s* c);
void http_upstream_break_off(struct http_connection_s* hc);
void http_upstream_destroy(http_upstream_t* u);

#endif
