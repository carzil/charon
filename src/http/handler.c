#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <linux/limits.h>

#include "http.h"
#include "http/handler.h"
#include "http/parser.h"
#include "defs.h"
#include "chain.h"
#include "utils/list.h"
#include "server.h"

const char CHARON_NOT_FOUND_PAGE[] = "<html><body><center><h1>Not found</h1></center><hr><center>charon " CHARON_VERSION "</center></body></html>";

int http_handler_on_read(event_t* ev);

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

void http_handler_make_response(connection_t* c)
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
    worker_enable_write(c);
}

static char* make_path(http_request_t* req, vhost_t* vhost)
{
    int pos = 0;
    if (buffer_string_copy(&vhost->path, req->uri.path)) {
        return NULL;
    }
    if (!string_cmpl(&req->uri.path, "/") && string_size(&req->uri.path) == 1) {
        memcpy(vhost->path.last, "index.html", sizeof("index.html"));
        pos = sizeof("index.html");
    }
    vhost->path.last[string_size(&req->uri.path) + pos] = '\0';
    return vhost->path.start;

}

static int process_vhost_request(connection_t* c, vhost_t* vhost)
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
    http_handler_make_response(c);
    http_request_destroy(&ctx->request);
    c->read_ev.handler = http_handler_on_read;
    return CHARON_OK;
}

int http_handler_process_request(connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_conf_t* conf = (http_conf_t*) c->handler->conf;
    string_t vhost_name = ctx->request.host;

    if (string_size(&vhost_name) == 0) {
        charon_info("request without host!");
        worker_stop_connection(c);
        return CHARON_OK;
    }

    for (size_t i = 0; i < vector_size(&conf->vhosts); i++) {
        vhost_t* vhost = &conf->vhosts[i];
        if (!string_cmp(&vhost->name, &vhost_name)) {
            charon_debug("request to host '%s'", vhost->name.start);
            return process_vhost_request(c, vhost);
        }
    }
    /* we didn't found appropriate vhost */
    worker_stop_connection(c);
    return CHARON_OK;
}

int http_handler_on_read_body(event_t* ev)
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
            worker_stop_connection(c);
            return CHARON_OK;
        }

        if (ctx->body_buf.start == NULL) {
            buffer_malloc(&ctx->body_buf, body_size);
        }
        count = conn_read(c, &ctx->body_buf);
        if (count < 0 || c->eof) {
            worker_stop_connection(c);
        } else if (body_size == (size_t)(&ctx->body_buf.last - &ctx->body_buf.start)) {
            goto process_request;
        }
        return CHARON_OK;
    }

process_request:
    return http_handler_process_request(c);
}

int http_handler_on_read(event_t* ev)
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
        worker_stop_connection(c);
        return CHARON_OK;
    } else if (count == 0 && c->eof) {
        worker_stop_connection(c);
        return CHARON_OK;
    }

    charon_debug("readed %d bytes from fd=%d", count, c->fd);
    charon_debug("content:\n===== [ request dump ] =====\n%*s===== [ request dump ] =====", (int)(ctx->req_buf.last - ctx->req_buf.start), ctx->req_buf.start);
    res = http_parser_feed(&ctx->parser, &ctx->req_buf, &ctx->request);
    if (res < 0) {
        worker_stop_connection(c);
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
        return http_handler_on_read_body(ev);
    }
    return CHARON_OK;
}

int http_handler_on_write(event_t* ev)
{
    connection_t* c = ev->data;
    int res = conn_write(c, &c->chain);
    if (res != CHARON_AGAIN && res != CHARON_OK) {
        worker_stop_connection(c);
    } else if (res == CHARON_OK) {
        worker_disable_write(c);
    }
    return CHARON_OK;
}

void http_handler_on_connection_end(connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_parser_destroy(&ctx->parser);
    buffer_destroy(&ctx->req_buf);
    buffer_destroy(&ctx->body_buf);
    worker_delayed_event_remove(c->worker, &c->timeout_ev);
    free(ctx);
}

int http_handler_on_timeout(event_t* ev)
{
    worker_stop_connection(event_connection(ev));
    return CHARON_OK;
}

int http_handler_connection_init(connection_t* c)
{
    c->context = malloc(sizeof(http_context_t));
    http_context_t* ctx = http_context(c->context);
    http_conf_t* conf = c->handler->conf;

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
    worker_enable_read(c);
    worker_delayed_event_push(c->worker, &c->timeout_ev, get_current_msec() + conf->main.accept_timeout * 1000);

    return CHARON_OK;
}

static conf_field_def_t vhost_fields_def[] = {
    { "root", CONF_STRING, offsetof(vhost_t, root) },
    { "name", CONF_STRING, offsetof(vhost_t, name) },
    { NULL, 0, 0 }
};

static conf_field_def_t main_fields_def[] = {
    { "accept_timeout", CONF_TIME_INTERVAL, offsetof(http_main_conf_t, accept_timeout) },
    { NULL, 0, 0 }
};

static conf_section_def_t conf_def[] = {
    { "vhost", CONF_ALLOW_MULTIPLE, vhost_fields_def, sizeof(vhost_t), offsetof(http_conf_t, vhosts) },
    { "main", 0, main_fields_def, sizeof(http_main_conf_t), offsetof(http_conf_t, main) },
    { NULL, 0, NULL, 0, 0},
};

void http_handler_on_config_done(handler_t* handler)
{
    http_handler_t* http_handler = (http_handler_t*) handler;
    http_conf_t* conf = handler->conf;

    charon_debug("%ld", conf->main.accept_timeout);

    for (size_t i = 0; i < vector_size(&conf->vhosts); i++) {
        vhost_t* vhost = &conf->vhosts[i];
        charon_debug("vhost '%s' at '%s'", vhost->name.start, vhost->root.start);
        buffer_malloc(&vhost->path, PATH_MAX);
        buffer_t* buf = &vhost->path;
        strcpy(buf->start, conf->vhosts->root.start);
        buffer_string_copy(buf, conf->vhosts->root);
        buf->last = buf->start + string_size(&vhost->root);
        if (*(buf->last - 1) != '/') {
            *(buf->last - 1) = '/';
        }
    }

}

http_handler_t* http_handler_on_init()
{
    http_handler_t* http_handler = malloc(sizeof(http_handler_t));
    handler_t* handler = (handler_t*) http_handler;
    handler->on_connection_init = http_handler_connection_init;
    handler->on_connection_end = http_handler_on_connection_end;
    handler->on_config_done = http_handler_on_config_done;
    handler->conf_def = conf_def;
    handler->conf = malloc(sizeof(http_conf_t));

    return http_handler;
}

void http_handler_on_finish(http_handler_t* h)
{
    free(h);
}
