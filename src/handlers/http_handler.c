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
#include "utils/list.h"

const char CHARON_NOT_FOUND_PAGE[] = "<html><body><center><h1>Not found</h1></center><hr><center>charon " CHARON_VERSION "</center></body></html>";

int http_handler_on_read(worker_t* s, event_t* ev);

void http_handler_on_finish(UNUSED worker_t* s)
{

}

buffer_t* make_status_line(http_response_t* resp)
{
    buffer_t* buf = buffer_create();
    buffer_malloc(buf, 512);
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
    write_header_i("Content-Length", resp->content_length, buf);
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
    buffer_clean(&ctx->req_buf);
    http_parser_init(&ctx->parser);
    worker_enable_write(s, c);
}

static char* make_path(http_request_t* req, vhost_t* vhost)
{
    if (buffer_string_copy(&vhost->path, req->uri.path)) {
        return NULL;
    }
    vhost->path.last[string_size(&req->uri.path)] = '\0';
    return vhost->path.start;

}

static int process_vhost_request(worker_t* s, connection_t* c, vhost_t* vhost)
{
    int fd;
    struct stat st;

    http_context_t* ctx = http_context(c->context);

    ctx->response.body_buf = buffer_create();
    ctx->response.body_buf->pos = 0;
    char* path = make_path(&ctx->request, vhost);
    charon_debug("path = '%s'", vhost->path.start);
    if (stat(path, &st) < 0) {
        http_response_set_status(&ctx->response, HTTP_NOT_FOUND);
        ctx->response.content_length = sizeof(CHARON_NOT_FOUND_PAGE);
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
        ctx->response.content_length = st.st_size;
        ctx->response.body_buf->fd = fd;
        ctx->response.body_buf->size = st.st_size;
    }
    http_handler_make_response(s, c);
    http_request_destroy(&ctx->request);
    c->read_ev.handler = http_handler_on_read;
    return CHARON_OK;
}

int http_handler_process_request(worker_t* w, connection_t* c)
{
    struct list_node* ptr;
    http_context_t* ctx = http_context(c->context);
    string_t vhost_name = ctx->request.host;
    if (string_size(&vhost_name) == 0) {
        charon_info("request without host!");
        worker_stop_connection(w, c);
        return CHARON_OK;
    }
    list_foreach(&w->conf->vhosts, ptr) {
        vhost_t* vhost = list_entry(ptr, vhost_t, lnode);
        if (!string_cmp(&vhost->name, &vhost_name)) {
            charon_debug("request to host '%s'", vhost->name.start);
            return process_vhost_request(w, c, vhost);
        }
    }
    /* we didn't found appropriate vhost */
    worker_stop_connection(w, c);
    return CHARON_OK;
}

int http_handler_on_read_body(worker_t* s, event_t* ev)
{
    int count;
    connection_t* c = ev->data;
    http_context_t* ctx = http_context(c->context);
    size_t body_size = ctx->request.content_length;
    if (body_size == 0) {
        goto process_request;
    } else {
        if (body_size > 4096) {
            charon_debug("client sent too big body size (%zu bytes)", body_size);
            worker_stop_connection(s, c);
            return CHARON_OK;
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
        return CHARON_OK;
    }

process_request:
    return http_handler_process_request(s, c);
}

int http_handler_on_read(worker_t* s, event_t* ev)
{
    int count, res;
    connection_t* c = ev->data;
    http_context_t* ctx = http_context(c->context);

    if (ctx->req_buf.start == NULL) {
        buffer_malloc(&ctx->req_buf, 4096);
        ctx->parser.state = st_method_start;
    }

    count = conn_read(c, &ctx->req_buf);
    charon_debug("readed %d", count);
    if (count < 0) {
        if (count == -CHARON_BUFFER_FULL) {
            charon_debug("client sent too big request (addr=%s)", c->hbuf);
        }
        worker_stop_connection(s, c);
        return CHARON_OK;
    } else if (count == 0 && c->eof) {
        worker_stop_connection(s, c);
        return CHARON_OK;
    }

    charon_debug("readed %d bytes from fd=%d", count, c->fd);
    charon_debug("content:\n===== [ request dump ] =====\n%*s===== [ request dump ] =====", (int)(ctx->req_buf.last - ctx->req_buf.start), ctx->req_buf.start);
    res = http_parser_feed(&ctx->parser, &ctx->req_buf, &ctx->request);
    if (res < 0) {
        worker_stop_connection(s, c);
        return CHARON_OK;
    } else if (res == HTTP_PARSER_BODY_START) {
        charon_info("readed request, method=%d, http version=%d.%d, path='%.*s'",
            ctx->request.method,
            ctx->request.http_version.major,
            ctx->request.http_version.minor,
            (int)string_size(&ctx->request.uri.path),
            ctx->request.uri.path.start
        );
        ev->handler = http_handler_on_read_body;
        return http_handler_on_read_body(s, ev);
    }
    return CHARON_OK;
}

int http_handler_on_write(worker_t* worker, event_t* ev)
{
    connection_t* c = ev->data;
    int res = conn_write(c, &c->chain);
    if (res != CHARON_AGAIN && res != CHARON_OK) {
        worker_stop_connection(worker, c);
    } else if (res == CHARON_OK) {
        worker_disable_write(worker, c);
    }
    return CHARON_OK;
}

void http_handler_on_connection_end(UNUSED worker_t* s, connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_parser_destroy(&ctx->parser);
    buffer_destroy(&ctx->req_buf);
    buffer_destroy(&ctx->body_buf);
    worker_delayed_event_remove(s, &c->timeout_ev);
    free(ctx);
}

int http_handler_on_timeout(worker_t* worker, event_t* ev)
{
    worker_stop_connection(worker, ev->data);
    return CHARON_OK;
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

    event_set_connection(&c->read_ev, c);
    event_set_connection(&c->write_ev, c);
    event_set_connection(&c->timeout_ev, c);
    c->read_ev.handler = http_handler_on_read;
    c->write_ev.handler = http_handler_on_write;
    c->timeout_ev.handler = http_handler_on_timeout;
    worker_enable_read(worker, c);
    worker_delayed_event_push(worker, &c->timeout_ev, get_current_msec() + 5 * 1000);
}

void http_handler_on_init(worker_t* w)
{
    list_node_t* ptr;
    vhost_t* vhost;
    buffer_t* buf;

    list_foreach(&w->conf->vhosts, ptr) {
        vhost = list_entry(ptr, vhost_t, lnode);
        buf = &vhost->path;
        strcpy(buf->start, vhost->root.start);
        buffer_string_copy(buf, vhost->root);
        buf->last = buf->start + string_size(&vhost->root);
        if (*(buf->last - 1) != '/') {
            *(buf->last - 1) = '/';
        }
    }
}
