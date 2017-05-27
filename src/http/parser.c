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

void http_parser_init(http_parser_t* parser, http_parser_type_t type)
{
    parser->method_pos = 0;
    switch (type) {
    case HTTP_PARSE_REQUEST:
        parser->state = st_method_start;
        break;
    case HTTP_PARSE_RESPONSE:
        parser->state = st_spaces_http_version;
        break;
    }
}

int http_parse_version(http_parser_t* p, buffer_t* buf, http_version_t* version)
{
    char* buf_ptr;
    char ch;

    while (buf->pos < buffer_size_last(buf)) {
        buf_ptr = buf->start + buf->pos;
        ch = *buf_ptr;

        switch (p->state) {
        case st_spaces_http_version:
            if (ch != ' ') {
                version->major = buf->pos;
                version->minor = 0;
                http_parser_transit(st_spaces_http_version, st_http_version);
            } else {
                buf->pos++;
            }
            break;

        case st_http_version:
            if (buf->pos - version->major < 4) {
                buf->pos++;
            } else {
                if (!strncmp(buf->start + version->major, "HTTP", 4)) {
                    version->major = 0;
                    if (ch != '/') {
                        return -HTTP_PARSER_ERR;
                    }
                    buf->pos++;
                    http_parser_transit(st_http_version, st_http_version_major);
                } else {
                    return -HTTP_PARSER_ERR;
                }
            }
            break;

        case st_http_version_major:
            if (ch == '.') {
                http_parser_transit(st_http_version_major, st_http_version_minor);
            } else if ('0' <= ch && ch <= '9') {
                version->major *= 10;
                version->major += ch - '0';
            } else {
                charon_debug("invalid character in http version: '%c'", ch);
                return -HTTP_PARSER_ERR;
            }
            buf->pos++;
            break;

        case st_http_version_minor:
            if (isdigit(ch)) {
                version->minor *= 10;
                version->minor += ch - '0';
            } else {
                return HTTP_PARSER_DONE;
            }
            buf->pos++;
            break;

        default:
            charon_error("invalid state");
            return -HTTP_PARSER_ERR;
        }
    }

    return HTTP_PARSER_AGAIN;
}

int http_parse_status_line(http_parser_t* p, buffer_t* buf,
    http_status_t* status, string_t* status_message, http_version_t* version)
{
    int res;
    char* buf_ptr;
    char ch;

    while (buf->pos < buffer_size_last(buf)) {
        buf_ptr = buf->start + buf->pos;
        ch = *buf_ptr;

        switch (p->state) {
        case st_spaces_http_version:
        case st_http_version:
        case st_http_version_major:
        case st_http_version_minor:
            res = http_parse_version(p, buf, version);
            if (res == HTTP_PARSER_DONE) {
                http_parser_transit(st_http_version_minor, st_spaces_status);
            } else {
                return res;
            }
            break;

        case st_spaces_status:
            if (ch != ' ') {
                http_parser_transit(st_spaces_status, st_status);
            } else {
                buf->pos++;
            }
            break;

        case st_status:
            if (ch == ' ') {
                http_parser_transit(st_status, st_spaces_status_message);
            } else if (!isdigit(ch)) {
                charon_debug("invalid digit '%c'", ch);
                return -HTTP_PARSER_ERR;
            } else {
                *status *= 10;
                *status += ch - '0';
            }
            buf->pos++;
            break;

        case st_spaces_status_message:
            if (ch != ' ') {
                status_message->start = buf_ptr;
                http_parser_transit(st_spaces_status_message, st_status_message);
            } else {
                buf->pos++;
            }
            break;

        case st_status_message:
            if (ch == '\r') {
                status_message->end = buf_ptr;
                http_parser_transit(st_status_message, st_finish_status_line);
            }
            buf->pos++;
            break;

        case st_finish_status_line:
            if (ch != '\n') {
                return -HTTP_PARSER_ERR;
            }
            buf->pos++;
            http_parser_transit(st_finish_status_line, st_headers_start);
            return HTTP_PARSER_DONE;

        default:
            charon_error("invalid state");
            return -HTTP_PARSER_ERR;
        }
    }

    return HTTP_PARSER_AGAIN;
}

int http_parse_request_line(http_parser_t* p, buffer_t* buf,
    http_method_t* method, http_uri_t* uri, http_version_t* version)
{
    int res;
    char* buf_ptr;
    char ch;

    while (buf->pos < buffer_size_last(buf)) {
        buf_ptr = buf->start + buf->pos;
        ch = *buf_ptr;

        switch (p->state) {
        case st_method_start:
            if (ch == 'G') {
                *method = HTTP_GET;
            } else if (ch == 'P') {
                *method = HTTP_POST;
            } else {
                charon_error("invalid method");
                return -HTTP_PARSER_ERR;
            }
            buf->pos++;
            p->method_pos++;
            http_parser_transit(st_method_start, st_method);
            break;

        /* TODO: on little-endian systems it can be optimized as one machine word lookup */
        case st_method:
            if (HTTP_METHODS[*method][buf->pos] == '\0') {
                buf->pos++;
                http_parser_transit(st_method, st_spaces_uri);
            } else if (ch == HTTP_METHODS[*method][p->method_pos]) {
                buf->pos++;
                p->method_pos++;
            } else {
                charon_error("invalid method");
                return -HTTP_PARSER_ERR;
            }
            break;

        case st_spaces_uri:
            if (ch != ' ') {
                http_parser_transit(st_spaces_uri, st_uri_start);
            } else {
                buf->pos++;
            }
            break;

        /* TODO: parse uri in separate structure */
        case st_uri_start:
            if (ch == '/') {
                uri->path.start = buf_ptr;
                http_parser_transit(st_uri_start, st_uri_path);
            } else {
                charon_error("expected absolute uri in request");
                return -HTTP_PARSER_ERR;
            }
            break;

        case st_uri_path:
            if (ch == ' ') {
                uri->path.end = buf_ptr;
                http_parser_transit(st_uri_path, st_spaces_http_version);
            }
            buf->pos++;
            break;

        case st_spaces_http_version:
        case st_http_version:
        case st_http_version_major:
        case st_http_version_minor:
            res = http_parse_version(p, buf, version);
            if (res == HTTP_PARSER_DONE) {
                if (buf->start[buf->pos] == '\r') {
                    http_parser_transit(st_http_version_minor, st_finish_request_line);
                } else {
                    return -HTTP_PARSER_ERR;
                }
            } else {
                return res;
            }
            buf->pos++;
            break;

        case st_finish_request_line:
            if (ch != '\n') {
                return -HTTP_PARSER_ERR;
            } else {
                http_parser_transit(st_finish_request_line, st_headers_start);
                buf->pos++;
            }
            return HTTP_PARSER_DONE;
        default:
            charon_error("invalid state");
            return -HTTP_PARSER_ERR;
        }
    }

    return HTTP_PARSER_AGAIN;
}

int http_parse_header(http_parser_t* p, buffer_t* buf, http_header_t* header)
{
    char* buf_ptr;
    char ch;

    while (buf->pos < buffer_size_last(buf)) {
        buf_ptr = buf->start + buf->pos;
        ch = *buf_ptr;

        switch (p->state) {
        case st_headers_start:
            http_parser_transit(st_headers_start, st_header_start);
            break;

        case st_header_start:
            if (ch == '\r') {
                buf->pos++;
                http_parser_transit(st_header_start, st_headers_end);
            } else {
                header->name.start = buf_ptr;
                http_parser_transit(st_header_start, st_header_name);
            }
            break;

        case st_header_name:
            if (ch == ':') {
                header->name.end = buf_ptr;
                buf->pos++;
                http_parser_transit(st_header_name, st_spaces_header_value);
            } else {
                buf->pos++;
            }
            break;

        case st_spaces_header_value:
            if (ch != ' ') {
                header->value.start = buf_ptr;
                http_parser_transit(st_spaces_header_value, st_header_value);
            } else {
                buf->pos++;
            }
            break;

        case st_header_value:
            if (ch == '\r') {
                header->value.end = buf_ptr;
                http_parser_transit(st_header_value, st_header_end);
            }
            buf->pos++;
            break;

        case st_header_end:
            if (ch == '\n') {
                http_parser_transit(st_header_end, st_header_start);
                buf->pos++;
                return HTTP_PARSER_OK;
            } else {
                return -HTTP_PARSER_ERR;
            }
            break;

        case st_headers_end:
            if (ch != '\n') {
                return -HTTP_PARSER_ERR;
            } else {
                buf->pos++;
                http_parser_transit(st_headers_end, st_body_start);
                return HTTP_PARSER_DONE;
            }
            break;

        default:
            charon_error("invalid state");
            return -CHARON_ERR;
        }
    }
    return HTTP_PARSER_AGAIN;
}

void http_parser_destroy(UNUSED http_parser_t* parser)
{
}
