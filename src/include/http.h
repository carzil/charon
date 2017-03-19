#ifndef _HTTP_
#define _HTTP_

#include <stddef.h>
#include "utils/string.h"
#include "utils/vector.h"
#include "chain.h"

enum {
    HTTP_GET = 0,
    HTTP_POST = 1,
};

enum {
    HTTP_OK = 200,
    HTTP_NOT_FOUND = 404,
    HTTP_SERVER_ERROR = 500,
};

#define HTTP_OK_MSG "OK"
#define HTTP_NOT_FOUND_MSG "Not Found"
#define HTTP_SERVER_ERROR_MSG "Internal Server Error"

typedef int http_method_t;
typedef int http_status_t;
typedef struct http_body_s http_body_t;
typedef struct http_version_s http_version_t;
typedef struct http_uri_s http_uri_t;
typedef struct http_header_s http_header_t;
typedef struct http_headers_container_s http_headers_container_t;
typedef struct http_response_s http_response_t;

struct http_version_s {
    int major;
    int minor;
};

struct http_uri_s {
    struct string scheme;
    struct string host;
    int port;
    struct string path;
};

struct http_header_s {
    string_t name;
    string_t value;
};

#define HTTP_EMPTY_HEADER (http_header_t) { STRING_EMPTY, STRING_EMPTY }

struct http_headers_container_s {
    size_t content_length;

    enum {
        CLOSE,
        KEEP_ALIVE
    } connection;

    struct string server;

    struct vector extra; // vector of struct http_header
};

struct http_request_s {
    int method;
    http_uri_t uri;
    http_version_t http_version;

    size_t body_size;
    chain_t body;

    struct http_headers_container_s headers;

    int parsed:1;
};

struct http_response_s {
    int status;
    char* status_message;
    http_version_t http_version;
    buffer_t* body_buf;
    struct http_headers_container_s headers;
};

typedef struct http_request_s http_request_t;
typedef struct http_response_s http_response_t;

static inline void http_headers_container_destroy(http_headers_container_t* container)
{
    vector_destroy(&container->extra);
}

static inline void http_request_init(http_request_t* req)
{
    req->headers.content_length = 0;
}

static inline void http_request_destroy(UNUSED http_request_t* req)
{
    chain_destroy(&req->body);
    http_headers_container_destroy(&req->headers);
}

static inline void http_response_init(http_response_t* resp)
{
    resp->headers.content_length = 0;
}

static inline void http_response_destroy(UNUSED http_response_t* resp)
{

}



#define http_response_set_status(resp, st) do { (resp)->status = st; (resp)->status_message = st##_MSG; } while (0);

#endif
