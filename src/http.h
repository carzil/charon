#ifndef _HTTP_
#define _HTTP_

#include <stddef.h>
#include "utils/string.h"
#include "chain.h"

#define HTTP_GET 0
#define HTTP_POST 1

#define HTTP_STATUS_OK 200
#define HTTP_STATUS_NOT_FOUND 404
#define HTTP_STATUS_SERVER_ERROR 500

struct http_body_s {
    char* buf;
    size_t total_size;
};

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
    int type;
    char* name;
    char* value;
};

#define HTTP_EMPTY_HEADER (http_header_t) { 0, NULL, NULL }

struct http_headers_container_s {
    size_t content_length;

    enum {
        CLOSE,
        KEEP_ALIVE
    } connection;

    struct string server;

    struct vector extra_header; // vector of struct http_header
};

struct http_response_s {
    int status;
    struct http_headers_container_s headers;
    chain_t out_chain;
};

typedef struct http_body_s http_body_t;
typedef struct http_version_s http_version_t;
typedef struct http_uri_s http_uri_t;
typedef struct http_header_s http_header_t;
typedef struct http_headers_container_s http_headers_container_t;
typedef struct http_response_s http_response_t;

#endif
