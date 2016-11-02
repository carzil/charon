#include <string.h>
#include <stdlib.h>

#include "http_parser.h"
#include "utils/logging.h"
#include "utils/string.h"

static const char* HTTP_METHODS[] = { "GET", "POST" };

struct http_request* http_request_create() {
    struct http_request* request = (struct http_request*) malloc(sizeof(struct http_request));
    request->body.total_size = 0;
    request->node = LIST_NODE_EMPTY;
    array_init(&request->buf, 20); // TODO: move to config
    return request;
}

void http_parser_init(struct http_parser* parser) {
    parser->request_queue = LIST_EMPTY;
    parser->pos = 0;
}

struct http_parser* http_parser_create() {
    struct http_parser* parser = (struct http_parser*) malloc(sizeof(struct http_parser));
    http_parser_init(parser);
    return parser;
}

int http_parser_parse_buffer(struct http_parser* parser, struct http_request* request, char* buffer, size_t size) {
    array_append(&request->buf, buffer, size);
    size = array_size(&request->buf);
    // TODO: move to config
    if (size > 1 << 20) {
        return -HTTP_PARSER_BUFFER_OVERFLOW;
    } else {
        struct http_header header = HTTP_EMPTY_HEADER;
        const char* svalue;

        while (parser->pos < size) {
            char ch = request->buf.data[parser->pos];
            char* buf_ptr = request->buf.data + parser->pos;
            switch (parser->state) {

                /* TODO: HTTP specifications presumes extra CRLFs before request line */
                case st_method_start:
                    if (ch == 'G') {
                        request->method = HTTP_GET;
                    } else if (ch == 'P') {
                        request->method = HTTP_POST;
                    } else {
                        return -HTTP_PARSER_INVALID_METHOD;
                    }
                    parser->pos++;
                    parser->state = st_method;
                    charon_debug("hp_parser: %d", request->method);
                    charon_debug("hp_state: st_method_start -> st_method");
                    break;

                /* TODO: on little endian systems it can be optimized as one machine word lookup */
                case st_method:
                    if (HTTP_METHODS[request->method][parser->pos] == '\0') {
                        charon_debug("parsed method '%s'", HTTP_METHODS[request->method]);
                        parser->pos++;
                        parser->state = st_spaces_uri;
                        charon_debug("hp_state: st_method_parse -> st_spaces_uri");
                    } else if (ch == HTTP_METHODS[request->method][parser->pos]) {
                        parser->pos++;
                    } else {
                        return -HTTP_PARSER_INVALID_METHOD;
                    }
                    break;

                case st_spaces_uri:
                    if (ch != ' ') {
                        parser->state = st_uri_start;
                        charon_debug("hp_state: st_spaces_uri -> st_uri_start");
                    } else {
                        parser->pos++;
                    }
                    break;

                /* TODO: parse uri in separate structure */
                /* TODO: don't copy string, just save pointers */
                /* TODO: another types of URI handling */
                case st_uri_start:
                    if (ch == '/') {
                        parser->state = st_uri_path;
                        request->uri.path.start = buf_ptr;
                    } else {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    }
                    break;

                case st_uri_path:
                    if (ch == ' ') {
                        request->uri.path.end = buf_ptr;
                        charon_debug("hp_parser: parsed uri: '%.*s'", string_size(&request->uri.path), request->uri.path.start);
                        charon_debug("hp_state: st_uri_host -> st_http_version_spaces");
                        parser->state = st_spaces_http_version;
                    }   
                    parser->pos++;
                    break;

                case st_spaces_http_version:
                    if (ch != ' ') {
                        parser->state = st_http_version;
                        charon_debug("hp_state: st_spaces_http_version -> st_http_version");
                        request->http_version.major = parser->pos;
                        request->http_version.minor = 0;
                    } else {
                        parser->pos++;
                    }
                    break;

                case st_http_version:
                    if (parser->pos - request->http_version.major < 4) {
                        parser->pos++;
                    } else {
                        if (!strncmp(request->buf.data + request->http_version.major, "HTTP", 4)) {
                            request->http_version.major = 0;
                            if (ch != '/') {
                                return -HTTP_PARSER_INVALID_REQUEST;
                            }
                            parser->state = st_http_version_major;
                            parser->pos++;
                            charon_debug("hp_state: st_http_version -> st_http_version_major");
                        } else {
                            return -HTTP_PARSER_INVALID_REQUEST;
                        }
                    }
                    break;

                case st_http_version_major:
                    if (ch == '.') {
                        parser->state = st_http_version_minor;
                        charon_debug("hp_state: st_http_version_major -> st_http_version_minor");
                    } else if ('0' <= ch && ch <= '9') {
                        request->http_version.major *= 10;
                        request->http_version.major += ch - '0';
                    } else {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    }
                    parser->pos++;
                    break;

                case st_http_version_minor:
                    if (ch == '\r') {
                        parser->state = st_headers_start;
                        charon_debug("hp_state: st_http_version_minor -> st_finish_request_line");
                        charon_debug("hp_parser: version is HTTP/%d.%d", request->http_version.major, request->http_version.minor);
                        parser->state = st_finish_request_line;
                    } else if ('0' <= ch && ch <= '9') {
                        request->http_version.minor *= 10;
                        request->http_version.minor += ch - '0';
                    } else {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    }
                    parser->pos++;
                    break;

                case st_finish_request_line:
                    if (ch != '\n') {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    } else {
                        charon_debug("hp_state: st_finish_request_line -> st_headers_start");
                        parser->state = st_headers_start;
                        parser->pos++;
                    }
                    break;

                case st_headers_start:
                    // TODO: move to config
                    if (vector_init(&request->headers, struct http_header, 3) < 0) {
                        return -HTTP_PARSER_NO_MEM;
                    }
                    parser->state = st_header_start;
                    charon_debug("hp_state: st_headers_start -> st_header_start");
                    break;

                case st_header_start:
                    if (ch == '\r') {
                        parser->state = st_headers_end;
                        parser->pos++;
                        charon_debug("hp_state: st_header_start -> st_headers_end");
                    } else {
                        header = HTTP_EMPTY_HEADER;
                        header.name = request->buf.data + parser->pos;
                        parser->state = st_header_name;
                        charon_debug("hp_state: st_header_start -> st_header_name");
                    }
                    break;

                case st_header_name:
                    if (ch == ':') {
                        size_t size = request->buf.data + parser->pos - header.name;
                        header.name = copy_string_z(header.name, size);
                        if (!header.name) {
                            return -HTTP_PARSER_NO_MEM;
                        }
                        parser->pos++;
                        charon_debug("hp_state: st_header_name -> st_header_value_spaces");
                        parser->state = st_spaces_header_value;
                    } else {
                        parser->pos++;
                    }
                    break;

                case st_spaces_header_value:
                    if (ch != ' ') {
                        header.value = request->buf.data + parser->pos;
                        charon_debug("hp_state: st_spaces_header_value -> st_header_value");
                        parser->state = st_header_value;
                    } else {
                        parser->pos++;
                    }
                    break;

                case st_header_value:
                    if (ch == '\r') {
                        size_t size = request->buf.data + parser->pos - header.value;
                        header.value = copy_string_z(header.value, size);
                        charon_debug("hp_state: st_header_value -> st_header_end");
                        parser->state = st_header_end;
                    }
                    parser->pos++;
                    break;

                case st_header_end:
                    if (ch == '\n') {
                        charon_debug("read header name='%s' value='%s'", header.name, header.value);
                        charon_debug("hp_state: st_header_end -> st_header_start");
                        parser->pos++;
                        parser->state = st_header_start;
                        // TODO: store common request headers in separate http_request fields for speedup
                        vector_push(&request->headers, &header, struct http_header);
                    } else {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    }
                    break;

                case st_headers_end:
                    if (ch != '\n') {
                        return -HTTP_PARSER_INVALID_REQUEST;
                    } else {
                        parser->state = st_body_start;
                        charon_debug("hp_state: st_headers_end -> st_body_start");
                    }
                    break;

                case st_body_start:
                    parser->pos++;
                    svalue = http_request_header_value(request, "Content-Length");
                    charon_debug("hp: svalue=%p", svalue);
                    if (svalue) {
                        charon_debug("hp: svalue=%s", svalue);
                    }
                    if (svalue == NULL) {
                        charon_debug("hp_state: st_body_start -> st_done");
                        return HTTP_PARSER_DONE_REQUEST;
                    } else {
                        int size = itoa(svalue);
                        // TODO: where we need to check max body size?
                        if (size > 0) {
                            request->body.total_size = size;
                            charon_debug("hp_state: st_headers_end -> st_body_start");
                        } else {
                            return -HTTP_PARSER_INVALID_REQUEST;
                        }
                    }
                    break;

                case st_body:
                    request->body.buf = request->buf.data + parser->pos;
                    parser->pos += request->body.total_size;
                    parser->state = st_body_end;
                    charon_debug("hp_state: st_body_start -> st_body_end");
                    break;

                case st_body_end:
                    request->body.buf = copy_string(request->body.buf, request->body.total_size);
                    if (!request->body.buf) {
                        return -HTTP_PARSER_NO_MEM;
                    }
                    charon_debug("hp_state: st_body_end -> st_done");
                    return HTTP_PARSER_DONE_REQUEST;

                case st_done:
                    return HTTP_PARSER_DONE_REQUEST;
            }
        }
    }
    return HTTP_PARSER_OK;
}

int http_parser_feed(struct http_parser* parser, char* buffer, size_t size) {
    struct list_node* node = list_tail(&parser->request_queue);
    struct http_request* request;

    if (!node || parser->state == st_done) {
        parser->state = st_method_start;
        request = http_request_create();
        list_append(&parser->request_queue, &request->node);
        charon_debug("parsing new request");
        charon_debug("hp_state: null -> st_method_start");
    } else {
        request = list_data(node, struct http_request);
    }

    // TODO: HTTP pipelining is not supported now
    int err = http_parser_parse_buffer(parser, request, buffer, size);
    if (err < 0) {
        charon_debug("hp_error: %d", -err);
    }

    if (err == HTTP_PARSER_DONE_REQUEST) {
        parser->state = st_done;
        request->parsed = 1;
    }
    return err;
}

void http_parser_free() {

}
