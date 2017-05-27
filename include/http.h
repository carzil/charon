#ifndef _HTTP_
#define _HTTP_

#include <stddef.h>
#include "utils/string.h"
#include "utils/vector.h"
#include "chain.h"
#include "http/status.h"

typedef struct http_body_s http_body_t;
typedef struct http_version_s http_version_t;
typedef struct http_uri_s http_uri_t;
typedef struct http_header_s http_header_t;
typedef struct http_headers_container_s http_headers_container_t;
typedef struct http_response_s http_response_t;
typedef struct http_request_s http_request_t;

#define HTTP_VERSION_10 ((http_version_t) { 1, 0 })
#define HTTP_VERSION_11 ((http_version_t) { 1, 1 })

struct http_version_s {
    int major;
    int minor;
};

static inline int http_version_equal(http_version_t a, http_version_t b)
{
    return a.major == b.major && a.minor == b.minor;
}

typedef enum {
    HTTP_GET,
    HTTP_POST
} http_method_t;

typedef enum {
    HTTP_OK = 200,
    HTTP_NOT_FOUND = 404,
    HTTP_BAD_REQUEST = 400,
    HTTP_INTERNAL_ERROR = 500,
    HTTP_BAD_GATEWAY = 502,
    HTTP_HEADER_TOO_LARGE = 431
} http_status_t;

struct http_uri_s {
    string_t scheme;
    string_t host;
    int port;
    string_t path;
};

struct http_header_s {
    string_t name;
    string_t value;
};

#define HTTP_EMPTY_HEADER (http_header_t) { STRING_EMPTY, STRING_EMPTY }

struct http_request_s {
    http_method_t method;
    http_uri_t uri;
    http_version_t version;

    size_t body_size;
    chain_t body;

    /* headers */
    VECTOR_DEFINE(headers, http_header_t);

    string_t host;
    string_t host_port;

    size_t content_length;

    enum {
        CLOSE,
        KEEP_ALIVE
    } connection;
};

struct http_response_s {
    http_status_t status;
    string_t status_message;
    http_version_t version;

    buffer_t* body_buf;

    /* headers */
    VECTOR_DEFINE(headers, http_header_t);
    string_t server;
    size_t content_length;
};

static inline void http_request_init(http_request_t* req)
{
    req->content_length = 0;
    vector_init(&req->headers);
}

static inline void http_request_destroy(UNUSED http_request_t* req)
{
    chain_destroy(&req->body);
    vector_destroy(&req->headers);
}

static inline void http_response_init(http_response_t* resp)
{
    resp->content_length = 0;
    resp->body_buf = NULL;
    resp->status = 0;
    vector_init(&resp->headers);
}

static inline void http_response_destroy(http_response_t* resp)
{
    vector_destroy(&resp->headers);
}

static inline void http_request_clean(http_request_t* req)
{
    vector_clean(&req->headers);
}

static inline void http_response_clean(http_response_t* resp)
{
    vector_clean(&resp->headers);
}

static inline void http_response_set_status(http_response_t* resp, http_status_t st) {
    resp->status = st;
    resp->status_message = http_get_status_message(st);
}

int write_header_s(string_t name, string_t value, buffer_t* buf);
int write_header_i(string_t name, int value, buffer_t* buf);
int http_write_status_line(buffer_t* buf, http_version_t version, http_status_t status, string_t status_message);

#endif
