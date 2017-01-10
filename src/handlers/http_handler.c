#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "http.h"
#include "http_handler.h"
#include "http_parser.h"
#include "defs.h"
#include "chain.h"

const char CHARON_NOT_FOUND_PAGE[] = "<html><body><center><h1>Not found</h1></center><hr><center>charon " CHARON_VERSION "</center></body></html>";

void http_handler_connection_init(connection_t* c)
{
    c->context = malloc(sizeof(http_context_t));
    http_context_t* ctx = http_context(c->context);
    http_parser_init(&ctx->parser);
    buffer_init(&ctx->req_buf);
    buffer_init(&ctx->body_buf);
    http_request_init(&ctx->request);
    http_response_init(&ctx->response);
    c->on_request = http_handler_on_request;
}

void http_handler_on_finish(charon_server* s)
{

}

void http_handler_on_request(charon_server* s, connection_t* c)
{
    int count, res;

    http_context_t* ctx = http_context(c->context);

    if (ctx->req_buf.start == NULL) {
        buffer_malloc(&ctx->req_buf, 4096);
        ctx->parser.state = st_method_start;
    }

    count = charon_conn_read(c, &ctx->req_buf);
    if (count < 0 || c->eof) {
        if (count == -CHARON_BUFFER_FULL) {
            charon_debug("client sent too big request (addr=%s)", c->hbuf);
        }
        charon_server_end_conn(s, c);
        return;
    }
    charon_debug("readed %d bytes from fd=%d", count, c->fd);
    charon_debug("content:\n===== [ request dump ] =====\n%*s===== [ request dump ] =====", ctx->req_buf.last - ctx->req_buf.start, ctx->req_buf.start);
    res = http_parser_feed(&ctx->parser, &ctx->req_buf, &ctx->request);
    if (res < 0) {
        charon_server_end_conn(s, c);
    } else if (res == HTTP_PARSER_BODY_START) {
        charon_debug("readed request, method=%d, http version=%d.%d, path='%.*s'",
            ctx->request.method,
            ctx->request.http_version.major,
            ctx->request.http_version.minor,
            string_size(&ctx->request.uri.path),
            ctx->request.uri.path.start
        );
        c->on_request = http_handler_on_request_body;
        http_handler_on_request_body(s, c);
    }


    if (c->eof) {
        charon_server_end_conn(s, c);
    }
}

void http_handler_on_request_body(charon_server* s, connection_t* c)
{
    int count;
    http_context_t* ctx = http_context(c->context);
    size_t body_size = ctx->request.headers.content_length;
    if (body_size == 0) {
        goto process_request;
    } else {
        if (body_size > 4096) {
            charon_debug("client sent too big body size (%d bytes)", body_size);
            charon_server_end_conn(s, c);
            return;
        }

        if (ctx->body_buf.start == NULL) {
            buffer_malloc(&ctx->body_buf, body_size);
        }
        count = charon_conn_read(c, &ctx->body_buf);
        if (count < 0 || c->eof) {
            charon_server_end_conn(s, c);
        } else if (body_size == &ctx->body_buf.last - &ctx->body_buf.start) {
            goto process_request;
        }
        return;
    }

    process_request:
    c->on_request = http_handler_on_request;
    http_handler_process_request(s, c);
    http_handler_make_response(s, c);
}

void http_handler_process_request(charon_server* s, connection_t* c)
{
    int fd;
    struct stat st;

    http_context_t* ctx = http_context(c->context);
    char* path = ctx->request.uri.path.start;
    path++;
    *ctx->request.uri.path.end = '\0';
    charon_debug("path = '%s'", path);
    if (stat(path, &st) < 0) {
        http_response_set_status(&ctx->response, HTTP_NOT_FOUND);
        ctx->response.headers.content_length = sizeof(CHARON_NOT_FOUND_PAGE);
        buffer_in_memory(&ctx->response.body_buf);
        ctx->response.body_buf.start = CHARON_NOT_FOUND_PAGE;
        ctx->response.body_buf.last = CHARON_NOT_FOUND_PAGE + sizeof(CHARON_NOT_FOUND_PAGE);
        ctx->response.body_buf.size = sizeof(CHARON_NOT_FOUND_PAGE);
    } else {
        fd = open(path, O_RDONLY);
        charon_debug("open('%s') = %d, size = %d", path, fd, st.st_size);
        http_response_set_status(&ctx->response, HTTP_OK);
        buffer_in_file(&ctx->response.body_buf);
        ctx->response.headers.content_length = st.st_size;
        ctx->response.body_buf.fd = fd;
        ctx->response.body_buf.pos = 0;
        ctx->response.body_buf.size = st.st_size;
    }
    *ctx->request.uri.path.end = ' ';
}

buffer_t* make_status_line(http_response_t* resp)
{
    buffer_t* buf = buffer_create();
    buffer_malloc(buf, 512);
    charon_debug("buf start: %p", buf->start);
    size_t size = snprintf(buf->start, 512, "HTTP/%d.%d %d %s\r\n",
        resp->http_version.major,
        resp->http_version.minor,
        resp->status,
        resp->status_message
    );
    buf->size = size;
    return buf;
}

void write_header_s(char* name, char* value, buffer_t* buf)
{
    buf->last += snprintf(buf->last, buf->end - buf->last, "%s: %s\r\n", name, value);
}

void write_header_i(char* name, int value, buffer_t* buf)
{
    buf->last += snprintf(buf->last, buf->end - buf->last, "%s: %d\r\n", name, value);
}

buffer_t* make_headers(http_response_t* resp)
{
    buffer_t* buf = buffer_create();
    buffer_malloc(buf, 4096);
    write_header_i("Content-Length", resp->headers.content_length, buf);
    write_header_s("Server", "charon/v" CHARON_VERSION, buf);
    *buf->last++ = '\r';
    *buf->last++ = '\n';
    buf->size = buf->last - buf->start;
    return buf;
}

void http_handler_make_response(charon_server* s, connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_response_t* resp = &ctx->response;
    resp->http_version.major = 1;
    resp->http_version.minor = 1;
    buffer_t* buf = make_status_line(resp);
    chain_push_buffer(&c->chain, buf);
    buf = make_headers(resp);
    chain_push_buffer(&c->chain, buf);
    chain_push_buffer(&c->chain, &resp->body_buf);
    chrn_conn_write(c, &c->chain);
    buffer_clean(&ctx->req_buf);
    http_parser_init(&ctx->parser);
}

void http_handler_on_connection_end(charon_server* s, connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_parser_destroy(&ctx->parser);
    buffer_destroy(&ctx->req_buf);
    buffer_destroy(&ctx->body_buf);
}
