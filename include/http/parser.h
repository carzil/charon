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
    HTTP_PARSER_DONE = 1,
    HTTP_PARSER_AGAIN = 2,
    HTTP_PARSER_ERR = 3,
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

    st_spaces_status,
    st_status,
    st_spaces_status_message,
    st_status_message,
    st_finish_status_line,

    st_body_start
} http_parser_state;

struct http_parser_s {
    http_parser_state state;
    size_t method_pos;
};

typedef enum {
    HTTP_PARSE_RESPONSE,
    HTTP_PARSE_REQUEST
} http_parser_type_t;

typedef struct http_parser_s http_parser_t;

void http_parser_init(http_parser_t* p, http_parser_type_t type);
int http_parse_request_line(http_parser_t* p, buffer_t* buf,
    http_method_t* method, http_uri_t* uri, http_version_t* version);
int http_parse_status_line(http_parser_t* p, buffer_t* buf,
    http_status_t* status, string_t* status_message, http_version_t* version);
int http_parse_header(http_parser_t* p, buffer_t* buf, http_header_t* header);
void http_parser_destroy(http_parser_t* p);

#endif
