#ifndef _HTTP_PARSER_
#define _HTTP_PARSER_

#include <stddef.h>

#include "utils/list.h"
#include "utils/array.h"
#include "utils/vector.h"
#include "utils/string.h"
#include "http.h"

#define HTTP_PARSER_OK 0
#define HTTP_PARSER_DONE_REQUEST 1
#define HTTP_PARSER_BUFFER_OVERFLOW 2
#define HTTP_PARSER_INVALID_METHOD 3
#define HTTP_PARSER_INVALID_REQUEST 4
#define HTTP_PARSER_NO_MEM 5

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

    st_body_start,
    st_body,
    st_body_end,

    st_done
} http_parser_state;

typedef int http_method_t;
typedef int http_status_t;
typedef char* http_uri_t;

struct http_header {
    char* name;
    char* value;
};

#define HTTP_EMPTY_HEADER (struct http_header) { NULL, NULL }

struct http_body {
    char* buf;
    size_t total_size;
};

struct http_version {
    int major;
    int minor;
};

struct http_uri {
    struct string scheme;
    struct string host;
    int port;
    struct string path;
};

struct http_request {
    struct list_node node;

    struct array buf;
    int method;
    struct http_uri uri;
    struct http_version http_version;
    struct http_body body;
    struct vector headers;

    int parsed; // TODO: request flags
};

static inline const char* http_request_header_value(struct http_request* r, const char* header_name) {
    for (size_t i = 0; i < vector_size(&r->headers); i++) {
        struct http_header* header = vector_data(&r->headers, i, struct http_header);
        if (!strcmp(header->name, header_name)) {
            return header->value;
        }
    }
    return NULL;
}

struct http_parser {
    size_t pos;
    http_parser_state state;
    struct list request_queue;
};

void http_parser_init(struct http_parser*);
struct http_parser* http_parser_create();
int http_parser_feed(struct http_parser* parser, char* buffer, size_t size);
void http_parser_free();

#endif
