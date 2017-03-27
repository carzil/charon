#ifndef _HTTP_PARSER_H_
#define _HTTP_PARSER_H_

#include <stddef.h>

#include "utils/list.h"
#include "utils/array.h"
#include "utils/vector.h"
#include "utils/string.h"
#include "http.h"

enum {
    HTTP_PARSER_OK = 0,
    HTTP_PARSER_DONE_REQUEST = 1,
    HTTP_PARSER_BUFFER_OVERFLOW = 2,
    HTTP_PARSER_INVALID_METHOD = 3,
    HTTP_PARSER_INVALID_REQUEST = 4,
    HTTP_PARSER_NO_MEM = 5,
    HTTP_PARSER_PASS = 6,
    HTTP_PARSER_BODY_START = 7,
    HTTP_PARSER_AGAIN = 8,
    HTTP_PARSER_ERROR = 9,
};

typedef enum {
    st_method_start,
    st_method,

    st_spaces_uri,
    st_uri_start,
    st_uri_path,

    st_spaces_http_version,
    st_http_version,
    st_http_version_major,
    st_http_version_minor,

    st_finish_request_line,

    st_headers_start,
    st_header_start,
    st_header_name,
    st_spaces_header_value,
    st_header_value,
    st_header_end,
    st_headers_end,

    st_body_start
} http_parser_state;

struct http_parser_s {
    int pos;
    http_parser_state state;
};

typedef struct http_parser_s http_parser_t;

void http_parser_init(http_parser_t* p);
http_parser_t* http_parser_create();
void http_parser_destroy(http_parser_t* parser);

int http_parser_feed(http_parser_t* p, buffer_t* buf, http_request_t* request);

#endif
