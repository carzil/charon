#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>

#include "http.h"
#include "handlers/http_handler.h"
#include "http_parser.h"
#include "defs.h"
#include "chain.h"

const char CHARON_NOT_FOUND_PAGE[] = "<html><body><center><h1>Not found</h1></center><hr><center>charon " CHARON_VERSION "</center></body></html>";

void http_handler_on_finish(UNUSED worker_t* s)
{

}

int http_handler_on_event(worker_t* worker, connection_t* c, event_t* ev)
{

    switch (ev->type) {
    case EV_TIMEOUT:
        worker_stop_connection(worker, c);
        return 0;
    }
    return 0;
}

void http_handler_process_request(UNUSED worker_t* s, connection_t* c)
{
    int fd;
    struct stat st;

    http_context_t* ctx = http_context(c->context);
    char* path = ctx->request.uri.path.start;
    path++;
    *ctx->request.uri.path.end = '\0';
    charon_debug("path = '%s'", path);
    ctx->response.body_buf = buffer_create();
    ctx->response.body_buf->pos = 0;
    if (stat(path, &st) < 0) {
        http_response_set_status(&ctx->response, HTTP_NOT_FOUND);
        ctx->response.headers.content_length = sizeof(CHARON_NOT_FOUND_PAGE);
        buffer_in_memory(ctx->response.body_buf);
        ctx->response.body_buf->start = (char*)CHARON_NOT_FOUND_PAGE;
        ctx->response.body_buf->end = (char*)(CHARON_NOT_FOUND_PAGE + sizeof(CHARON_NOT_FOUND_PAGE));
        ctx->response.body_buf->size = sizeof(CHARON_NOT_FOUND_PAGE);
    } else {
        fd = open(path, O_RDONLY);
        charon_debug("open('%s') = %d, size = %zu", path, fd, st.st_size);
        http_response_set_status(&ctx->response, HTTP_OK);
        buffer_in_file(ctx->response.body_buf);
        ctx->response.body_buf->owning = 1;
        ctx->response.headers.content_length = st.st_size;
        ctx->response.body_buf->fd = fd;
        ctx->response.body_buf->size = st.st_size;
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

void http_handler_make_response(worker_t* s, connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_response_t* resp = &ctx->response;
    resp->http_version.major = 1;
    resp->http_version.minor = 1;
    buffer_t* buf = make_status_line(resp);
    chain_push_buffer(&c->chain, buf);
    buf = make_headers(resp);
    chain_push_buffer(&c->chain, buf);
    chain_push_buffer(&c->chain, resp->body_buf);
    worker_schedule_write(s, c);
    buffer_clean(&ctx->req_buf);
    http_parser_init(&ctx->parser);
}

void http_handler_on_request_body(worker_t* s, connection_t* c)
{
    int count;
    http_context_t* ctx = http_context(c->context);
    size_t body_size = ctx->request.headers.content_length;
    if (body_size == 0) {
        goto process_request;
    } else {
        if (body_size > 4096) {
            charon_debug("client sent too big body size (%zu bytes)", body_size);
            worker_stop_connection(s, c);
            return;
        }

        if (ctx->body_buf.start == NULL) {
            buffer_malloc(&ctx->body_buf, body_size);
        }
        count = conn_read(c, &ctx->body_buf);
        if (count < 0 || c->eof) {
            worker_stop_connection(s, c);
        } else if (body_size == (size_t)(&ctx->body_buf.last - &ctx->body_buf.start)) {
            goto process_request;
        }
        return;
    }

process_request:
    c->on_request = http_handler_on_request;
    http_handler_process_request(s, c);
    http_handler_make_response(s, c);
    http_request_destroy(&ctx->request);
}

void http_handler_on_request(worker_t* s, connection_t* c)
{
    int count, res;

    http_context_t* ctx = http_context(c->context);

    if (ctx->req_buf.start == NULL) {
        buffer_malloc(&ctx->req_buf, 4096);
        ctx->parser.state = st_method_start;
    }

    count = conn_read(c, &ctx->req_buf);
    if (count < 0 || c->eof) {
        if (count == -CHARON_BUFFER_FULL) {
            charon_debug("client sent too big request (addr=%s)", c->hbuf);
        }
        worker_stop_connection(s, c);
        return;
    }
    charon_debug("readed %d bytes from fd=%d", count, c->fd);
    charon_debug("content:\n===== [ request dump ] =====\n%*s===== [ request dump ] =====", (int)(ctx->req_buf.last - ctx->req_buf.start), ctx->req_buf.start);
    res = http_parser_feed(&ctx->parser, &ctx->req_buf, &ctx->request);
    if (res < 0) {
        worker_stop_connection(s, c);
        return;
    } else if (res == HTTP_PARSER_BODY_START) {
        charon_debug("readed request, method=%d, http version=%d.%d, path='%.*s'",
            ctx->request.method,
            ctx->request.http_version.major,
            ctx->request.http_version.minor,
            (int)string_size(&ctx->request.uri.path),
            ctx->request.uri.path.start
        );
        c->on_request = http_handler_on_request_body;
        http_handler_on_request_body(s, c);
    }


    if (c->eof) {
        worker_stop_connection(s, c);
        return;
    }
    timer_queue_update(&s->timer_queue, c->timeout_event, get_current_msec() + 5 * 5000);
}

void http_handler_on_connection_end(UNUSED worker_t* s, connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_parser_destroy(&ctx->parser);
    buffer_destroy(&ctx->req_buf);
    buffer_destroy(&ctx->body_buf);
    free(ctx);
}

void http_handler_connection_init(worker_t* worker, connection_t* c)
{
    c->context = malloc(sizeof(http_context_t));
    http_context_t* ctx = http_context(c->context);
    http_parser_init(&ctx->parser);
    buffer_init(&ctx->req_buf);
    buffer_init(&ctx->body_buf);
    http_request_init(&ctx->request);
    http_response_init(&ctx->response);

    event_t* ev = event_create(c, EV_TIMEOUT);
    /* TODO: move to config */
    ev->expire = get_current_msec() + 5 * 1000;
    timer_queue_push(&worker->timer_queue, ev);
    c->timeout_event = ev;

    c->on_request = http_handler_on_request;
    c->on_event = http_handler_on_event;
}
