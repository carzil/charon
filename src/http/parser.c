#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "http/parser.h"
#include "utils/logging.h"
#include "utils/string.h"

static const char* HTTP_METHODS[] = { "GET", "POST" };

#define http_parser_transit(from, to)                   \
    do {                                                \
        p->state = to;                                  \
        charon_debug("hp_parser: " #from " -> " #to);   \
    } while (0)


http_request_t* http_request_create()
{
    http_request_t* request = (http_request_t*) malloc(sizeof(http_request_t));
    return request;
}

void http_parser_init(http_parser_t* parser)
{
    parser->pos = 0;
    parser->state = st_method_start;
}

http_parser_t* http_parser_create()
{
    http_parser_t* parser = (http_parser_t*) malloc(sizeof(http_parser_t));
    http_parser_init(parser);
    return parser;
}

int http_parser_handle_header(http_request_t* request, http_header_t* header)
{
    if (!string_cmpl(&header->name, "Content-Length")) {
        request->content_length = string_to_int(&header->value);
        return HTTP_PARSER_OK;
    } else if (!string_cmpl(&header->name, "Host")) {
        string_clone(&request->host, &header->value);
        char* ptr = header->value.start;
        while (++ptr < header->value.end && *ptr != ':');
        request->host.end = ptr;
        if (ptr++ < header->value.end) {
            request->host_port.start = ptr;
            while (ptr++ < header->value.end);
            request->host_port.end = ptr;
        }
        return HTTP_PARSER_OK;
    }
    return HTTP_PARSER_PASS;
}

int http_parser_feed(http_parser_t* p, buffer_t* buf, http_request_t* request) {
    int res;
    char* buf_ptr;
    char ch;
    http_header_t header;

    while (p->pos < buffer_size(buf)) {
        buf_ptr = buf->start + p->pos;
        ch = *buf_ptr;

        switch (p->state) {
        case st_method_start:
            if (ch == 'G') {
                request->method = HTTP_GET;
            } else if (ch == 'P') {
                request->method = HTTP_POST;
            } else {
                return -HTTP_PARSER_INVALID_METHOD;
            }
            p->pos++;
            http_parser_transit(st_method_start, st_method);
            break;

        /* TODO: on little-endian systems it can be optimized as one machine word lookup */
        case st_method:
            if (HTTP_METHODS[request->method][p->pos] == '\0') {
                p->pos++;
                http_parser_transit(st_method, st_spaces_uri);
            } else if (ch == HTTP_METHODS[request->method][p->pos]) {
                p->pos++;
            } else {
                return -HTTP_PARSER_INVALID_METHOD;
            }
            break;

        case st_spaces_uri:
            if (ch != ' ') {
                http_parser_transit(st_spaces_uri, st_uri_start);
            } else {
                p->pos++;
            }
            break;

        /* TODO: parse uri in separate structure */
        case st_uri_start:
            if (ch == '/') {
                request->uri.path.start = buf_ptr;
                http_parser_transit(st_uri_start, st_uri_path);
            } else {
                return -HTTP_PARSER_INVALID_REQUEST;
            }
            break;

        case st_uri_path:
            if (ch == ' ') {
                request->uri.path.end = buf_ptr;
                charon_debug("hp_parser: parsed uri: '%.*s'", (int)string_size(&request->uri.path), request->uri.path.start);
                http_parser_transit(st_uri_path, st_spaces_http_version);
            }
            p->pos++;
            break;

        case st_spaces_http_version:
            if (ch != ' ') {
                request->http_version.major = p->pos;
                request->http_version.minor = 0;
                http_parser_transit(st_spaces_http_version, st_http_version);
            } else {
                p->pos++;
            }
            break;

        case st_http_version:
            if (p->pos - request->http_version.major < 4) {
                p->pos++;
            } else {
                if (!strncmp(buf->start + request->http_version.major, "HTTP", 4)) {
                    request->http_version.major = 0;
                    if (ch != '/') {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    }
                    p->pos++;
                    http_parser_transit(st_http_version, st_http_version_major);
                } else {
                    return -HTTP_PARSER_INVALID_REQUEST;
                }
            }
            break;

        case st_http_version_major:
            if (ch == '.') {
                http_parser_transit(st_http_version_major, st_http_version_minor);
            } else if ('0' <= ch && ch <= '9') {
                request->http_version.major *= 10;
                request->http_version.major += ch - '0';
            } else {
                return -HTTP_PARSER_INVALID_REQUEST;
            }
            p->pos++;
            break;

        case st_http_version_minor:
            if (ch == '\r') {
                http_parser_transit(st_http_version_minor, st_finish_request_line);
                charon_debug("hp_parser: version is HTTP/%d.%d", request->http_version.major, request->http_version.minor);
            } else if ('0' <= ch && ch <= '9') {
                request->http_version.minor *= 10;
                request->http_version.minor += ch - '0';
            } else {
                return -HTTP_PARSER_INVALID_REQUEST;
            }
            p->pos++;
            break;

        case st_finish_request_line:
            if (ch != '\n') {
                return -HTTP_PARSER_INVALID_REQUEST;
            } else {
                http_parser_transit(st_finish_request_line, st_headers_start);
                p->pos++;
            }
            break;

        case st_headers_start:
            vector_init(&request->headers);
            http_parser_transit(st_headers_start, st_header_start);
            break;

        case st_header_start:
            if (ch == '\r') {
                p->pos++;
                http_parser_transit(st_header_start, st_headers_end);
            } else {
                header = HTTP_EMPTY_HEADER;
                header.name.start = buf_ptr;
                http_parser_transit(st_header_start, st_header_name);
            }
            break;

        case st_header_name:
            if (ch == ':') {
                header.name.end = buf_ptr;
                p->pos++;
                http_parser_transit(st_header_name, st_spaces_header_value);
            } else {
                p->pos++;
            }
            break;

        case st_spaces_header_value:
            if (ch != ' ') {
                header.value.start = buf_ptr;
                http_parser_transit(st_spaces_header_value, st_header_value);
            } else {
                p->pos++;
            }
            break;

        case st_header_value:
            if (ch == '\r') {
                header.value.end = buf_ptr;
                http_parser_transit(st_header_value, st_header_end);
            }
            p->pos++;
            break;

        case st_header_end:
            if (ch == '\n') {
                charon_debug("read header name='%.*s' value='%.*s'", (int)string_size(&header.name), header.name.start, (int)string_size(&header.value), header.value.start);
                http_parser_transit(st_header_end, st_header_start);
                p->pos++;
                res = http_parser_handle_header(request, &header);
                switch (res) {
                case HTTP_PARSER_PASS:
                    vector_push(&request->headers, &header, http_header_t);
                    break;
                case HTTP_PARSER_OK:
                    break;
                default:
                    return -HTTP_PARSER_INVALID_REQUEST;

                }
            } else {
                return -HTTP_PARSER_INVALID_REQUEST;
            }
            break;

        case st_headers_end:
            if (ch != '\n') {
                return -HTTP_PARSER_INVALID_REQUEST;
            } else {
                http_parser_transit(st_headers_end, st_body_start);
            }
            break;

        case st_body_start:
            p->pos++;
            return HTTP_PARSER_BODY_START;
        }
    }
    return HTTP_PARSER_AGAIN;
}

void http_parser_destroy(UNUSED http_parser_t* parser)
{
}
