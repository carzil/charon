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
#include "http/upstream.h"
#include "defs.h"
#include "chain.h"
#include "utils/list.h"
#include "worker.h"

const char CHARON_DEFAULT_ERRPAGE_HEADER[] = "<html><body><center><h1>";
const char CHARON_DEFAULT_ERRPAGE_FOOTER[] = "</h1></center><hr><center>charon " CHARON_VERSION "</center></body></html>\n";

int http_context_init(http_context_t* ctx)
{
    buffer_init(&ctx->req_buf);
    http_parser_init(&ctx->parser, HTTP_PARSE_REQUEST);
    http_request_init(&ctx->request);
    http_response_init(&ctx->response);

    return CHARON_OK;
}

buffer_t* make_head(http_response_t* resp)
{
    buffer_t* buf = buffer_create();
    buffer_malloc(buf, 4096);
    http_write_status_line(buf, resp->version, resp->status, resp->status_message);
    write_header_i(string("Content-Length"), resp->content_length, buf);
    write_header_s(string("Server"), string("charon/v" CHARON_VERSION), buf);
    *buf->last++ = '\r';
    *buf->last++ = '\n';
    buf->size = buf->last - buf->start;
    return buf;
}

void http_handler_make_response(connection_t* c)
{
    http_context_t* ctx = http_context(c->context);
    http_response_t* resp = &ctx->response;

    resp->version.major = 1;
    resp->version.minor = 1;

    buffer_t* buf = make_head(resp);
    chain_push_buffer(&c->chain, buf);
    if (resp->content_length > 0) {
        chain_push_buffer(&c->chain, resp->body_buf);
    }
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

int make_error_page(connection_t* c, http_status_t error)
{
    http_context_t* ctx = c->context;
    buffer_t* buf = ctx->response.body_buf;
    string_t status_msg = http_get_status_message(error);

    if (buf == NULL) {
        ctx->response.body_buf = buf = buffer_create();
    }

    size_t resp_sz = sizeof(CHARON_DEFAULT_ERRPAGE_HEADER) - 1 + sizeof(CHARON_DEFAULT_ERRPAGE_FOOTER) - 1;
    resp_sz += 3 + 1;
    resp_sz += string_size(&status_msg);

    /* FIXME: use realloc if size < resp_sz */
    if (buf->start == NULL) {
        buffer_malloc(buf, resp_sz);
    }

    if (buf->start == NULL) {
        return -CHARON_NO_MEM;
    }

    memcpy(buf->start, CHARON_DEFAULT_ERRPAGE_HEADER, sizeof(CHARON_DEFAULT_ERRPAGE_HEADER) - 1);
    buf->last += sizeof(CHARON_DEFAULT_ERRPAGE_HEADER) - 1;
    buf->last += snprintf(buf->last, 5, "%d ", error);
    memcpy(buf->last, status_msg.start, string_size(&status_msg));
    buf->last += string_size(&status_msg);
    memcpy(buf->last, CHARON_DEFAULT_ERRPAGE_FOOTER, sizeof(CHARON_DEFAULT_ERRPAGE_FOOTER) - 1);
    buf->last += sizeof(CHARON_DEFAULT_ERRPAGE_FOOTER) - 1;

    ctx->response.content_length = buffer_size_last(buf);
    buf->pos = 0;
    buf->size = buffer_size_last(buf);
    buffer_in_memory(buf);

    return CHARON_OK;
}

/* This routine is end-point of request processing pipeline.
 * if error is not 0, it forms an error response with code error.
 */
int http_end_process_request(connection_t* c, http_status_t error)
{
    http_context_t* ctx = http_context(c->context);

    if (error != 0) {
        http_response_set_status(&ctx->response, error);
        make_error_page(c, error);
    }

    http_handler_make_response(c);
    worker_enable_write(c);
    buffer_rewind(&ctx->req_buf);
    http_handler_cleanup_connection(c);

    return CHARON_OK;
}

int http_stop_connection(connection_t* c)
{
    http_context_t* ctx = http_context(c->context);

    if (ctx->req_buf.start + ctx->req_buf.pos < ctx->req_buf.last) {
        worker_defer_event(c->worker, &c->read_ev);
    } else {
        worker_disable_write(c);
        chain_clear(&c->chain);
        ctx->response.body_buf = NULL;
        if (ctx->request.connection == CLOSE || http_version_equal(ctx->request.version, HTTP_VERSION_10)) {
            worker_stop_connection(c);
        } else {
            worker_enable_read(c);
        }
    }

    return CHARON_OK;
}

static int process_vhost_local(connection_t* c, vhost_t* vhost)
{
    int fd;
    struct stat st;
    http_context_t* ctx = http_context(c->context);
    char* path = make_path(&ctx->request, vhost);

    charon_debug("path = '%s'", vhost->path.start);
    if (stat(path, &st) < 0) {
        return http_end_process_request(c, HTTP_NOT_FOUND);
    } else {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            return http_end_process_request(c, HTTP_INTERNAL_ERROR);
        } else {
            charon_debug("open('%s') = %d, size = %zu", path, fd, st.st_size);
            http_response_set_status(&ctx->response, HTTP_OK);
            buffer_in_file(ctx->response.body_buf);
            ctx->response.body_buf->owning = 1;
            ctx->response.content_length = st.st_size;
            ctx->response.body_buf->fd = fd;
            ctx->response.body_buf->size = st.st_size;
        }
    }
    return http_end_process_request(c, 0);
}

static int process_vhost_upstream(connection_t* c, vhost_t* vhost)
{
    http_upstream_connection_t* uc = http_upstream_connect(&vhost->upstream);
    if (uc == NULL) {
        charon_info("no available connection to upstream");
        return http_end_process_request(c, HTTP_BAD_GATEWAY);
    }
    return http_upstream_bond(uc, c);
}

static int process_vhost_request(connection_t* c, vhost_t* vhost)
{
    http_context_t* ctx = c->context;

    ctx->response.body_buf = buffer_create();
    ctx->response.body_buf->pos = 0;
    if (vhost->root.start != NULL) {
        return process_vhost_local(c, vhost);
    } else {
        return process_vhost_upstream(c, vhost);
    }
}

int http_handler_process_request(connection_t* c)
{
    http_context_t* ctx = c->context;
    http_conf_t* conf = c->handler->conf;
    string_t vhost_name = ctx->request.host;

    if (string_size(&vhost_name) == 0) {
        charon_info("request without host");
        return http_end_process_request(c, HTTP_BAD_REQUEST);
    }

    for (size_t i = 0; i < vector_size(&conf->vhosts); i++) {
        vhost_t* vhost = &conf->vhosts[i];
        if (!string_cmp(&vhost->name, &vhost_name)) {
            charon_debug("request to host '%s'", vhost->name.start);
            return process_vhost_request(c, vhost);
        }
    }

    /* we didn't found appropriate vhost */
    return http_end_process_request(c, HTTP_BAD_REQUEST);
}

static int handle_header(http_request_t* request, http_header_t* header)
{
    if (!string_cmpl(&header->name, "Content-Length")) {
        request->content_length = string_to_int(&header->value);
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
    } else if (!string_cmpl(&header->name, "Connection")) {
        if (!string_cmpl(&header->value, "keep-alive")) {
            request->connection = KEEP_ALIVE;
        } else {
            request->connection = CLOSE;
        }
    } else {
        vector_push(&request->headers, header, http_header_t);
    }
    return CHARON_OK;
}

int http_handler_on_read_headers(event_t* ev)
{
    int res, count;
    connection_t* c = ev->data;
    http_context_t* ctx = c->context;
    http_request_t* req = &ctx->request;

    count = conn_read(c, &ctx->req_buf);
    if (count < 0 && count != -CHARON_BUFFER_FULL) {
        worker_disable_read(c);
        return http_end_process_request(c, HTTP_INTERNAL_ERROR);
    }

    if (c->eof) {
        worker_stop_connection(c);
        return CHARON_OK;
    }

    for (;;) {
        res = http_parse_header(&ctx->parser, &ctx->req_buf, &ctx->header);
        if (res == HTTP_PARSER_OK) {
            handle_header(req, &ctx->header);
            charon_debug("parsed header name='%.*s' value='%.*s'",
                    (int)string_size(&ctx->header.name), ctx->header.name.start,
                    (int)string_size(&ctx->header.value), ctx->header.value.start
            );
        } else if (res == HTTP_PARSER_DONE) {
            charon_debug("parsed all headers");
            /* we read whole request header */
            worker_disable_read(c);
            return http_handler_process_request(c);
        } else {
            return http_end_process_request(c, HTTP_BAD_REQUEST);
        }
    }

    return CHARON_OK;
}

int http_handler_on_read_request_line(event_t* ev)
{
    int count, res;
    connection_t* c = ev->data;
    http_context_t* ctx = c->context;
    http_conf_t* conf = c->handler->conf;
    http_request_t* req = &ctx->request;

    worker_delayed_event_remove(c->worker, &c->timeout_ev);

    if (ctx->req_buf.start == NULL) {
        buffer_malloc(&ctx->req_buf, conf->main.client_buffer_size);
        http_parser_init(&ctx->parser, HTTP_PARSE_REQUEST);
    }

    count = conn_read(c, &ctx->req_buf);
    if (count < 0 && count != -CHARON_BUFFER_FULL) {
        worker_disable_read(c);
        return http_end_process_request(c, HTTP_INTERNAL_ERROR);
    }

    if (c->eof) {
        worker_stop_connection(c);
        return CHARON_OK;
    }

    charon_debug("readed %d bytes from fd=%d", count, c->fd);
    charon_debug("content:\n===== [ request dump ] =====\n%*s===== [ request dump ] =====", (int)(ctx->req_buf.last - ctx->req_buf.start), ctx->req_buf.start);

    res = http_parse_request_line(&ctx->parser, &ctx->req_buf, &req->method, &req->uri, &req->version);
    if (res == HTTP_PARSER_DONE) {
        charon_info("parsed status line, method=%d, http version=%d.%d, path='%.*s'",
            req->method,
            req->version.major,
            req->version.minor,
            (int)string_size(&ctx->request.uri.path),
            ctx->request.uri.path.start
        );
        c->read_ev.handler = http_handler_on_read_headers;
        return http_handler_on_read_headers(ev);
    } else if (res < 0) {
        ctx->request.connection = CLOSE;
        return http_end_process_request(c, HTTP_BAD_REQUEST);
    }

    /* request is still incomplete */
    if (count == -CHARON_BUFFER_FULL) {
        return http_end_process_request(c, HTTP_HEADER_TOO_LARGE);
    }

    return CHARON_OK;
}

int http_handler_on_write(event_t* ev)
{
    connection_t* c = ev->data;

    int res = conn_write(c, &c->chain);
    if (res != -CHARON_AGAIN && res != CHARON_OK) {
        worker_stop_connection(c);
    } else if (res == CHARON_OK) {
        http_stop_connection(c);
    }
    return CHARON_OK;
}

void http_handler_on_connection_end(connection_t* c)
{
    http_context_t* ctx = c->context;
    http_parser_destroy(&ctx->parser);
    http_request_destroy(&ctx->request);
    buffer_destroy(&ctx->req_buf);
    worker_delayed_event_remove(c->worker, &c->timeout_ev);
    worker_deferred_event_remove(c->worker, &c->read_ev);
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
    http_context_t* ctx = c->context;
    http_conf_t* conf = c->handler->conf;

    http_context_init(ctx);

    ctx->req_buf.start = NULL;

    event_set_connection(&c->read_ev, c);
    event_set_connection(&c->write_ev, c);
    event_set_connection(&c->timeout_ev, c);
    c->read_ev.handler = http_handler_on_read_request_line;
    c->write_ev.handler = http_handler_on_write;
    c->timeout_ev.handler = http_handler_on_timeout;
    worker_enable_read(c);
    worker_delayed_event_push(c->worker, &c->timeout_ev, get_current_msec() + conf->main.accept_timeout * 1000);

    return CHARON_OK;
}

static conf_field_def_t vhost_fields_def[] = {
    { "root", CONF_STRING, offsetof(vhost_t, root) },
    { "name", CONF_STRING, offsetof(vhost_t, name) },
    { "upstream", CONF_STRING, offsetof(vhost_t, upstream) + offsetof(http_upstream_t, uri) },
    { NULL, 0, 0 }
};

static conf_field_def_t main_fields_def[] = {
    { "accept_timeout", CONF_TIME_INTERVAL, offsetof(http_main_conf_t, accept_timeout) },
    { "client_buffer_size", CONF_SIZE, offsetof(http_main_conf_t, client_buffer_size) },
    { NULL, 0, 0 }
};

static conf_section_def_t conf_def[] = {
    { "vhost", CONF_ALLOW_MULTIPLE, vhost_fields_def, (conf_type_init_t) vhost_init, sizeof(vhost_t), offsetof(http_conf_t, vhosts) },
    { "main", 0, main_fields_def, NULL, sizeof(http_main_conf_t), offsetof(http_conf_t, main) },
    { NULL, 0, NULL, NULL, 0, 0},
};

void http_handler_on_config_done(handler_t* handler)
{
    http_conf_t* conf = handler->conf;

    for (size_t i = 0; i < vector_size(&conf->vhosts); i++) {
        vhost_t* vhost = &conf->vhosts[i];
        if (vhost->root.start) {
            charon_debug("vhost '%s' at '%s'", vhost->name.start, vhost->root.start);
            buffer_t* buf = &vhost->path;
            strcpy(buf->start, conf->vhosts->root.start);
            buffer_string_copy(buf, conf->vhosts->root);
            buf->last = buf->start + string_size(&vhost->root);
            if (*(buf->last - 1) != '/') {
                *(buf->last - 1) = '/';
            }
        } else if (vhost->upstream.uri.start) {
            charon_debug("vhost '%s' at '%s'", vhost->name.start, vhost->upstream.uri.start);
            char* ch = vhost->upstream.uri.start;
            while (ch != vhost->upstream.uri.end && *ch != ':') {
                ch++;
            }
            *ch++ = '\0';
            charon_debug("%s", vhost->upstream.uri.start);
        }
    }
}

void http_handler_cleanup_connection(connection_t* c)
{
    http_context_t* ctx = c->context;

    c->write_ev.handler = http_handler_on_write;
    c->read_ev.handler = http_handler_on_read_request_line;
    http_parser_init(&ctx->parser, HTTP_PARSE_REQUEST);
    http_request_clean(&ctx->request);
    http_response_clean(&ctx->response);
    buffer_rewind(&ctx->req_buf);
}

http_conf_t* http_conf_create()
{
    return malloc(sizeof(http_conf_t));
}

void http_conf_destroy(http_conf_t* conf)
{
    for (size_t i = 0; i < vector_size(&conf->vhosts); i++) {
        vhost_destroy(&conf->vhosts[i]);
    }
    vector_destroy(&conf->vhosts);
}

http_handler_t* http_handler_on_init()
{
    http_handler_t* http_handler = malloc(sizeof(http_handler_t));
    handler_t* handler = (handler_t*) http_handler;
    handler->on_connection_init = http_handler_connection_init;
    handler->on_connection_end = http_handler_on_connection_end;
    handler->on_config_done = http_handler_on_config_done;
    handler->conf_def = conf_def;
    handler->conf = http_conf_create();

    return http_handler;
}

void http_handler_on_finish(http_handler_t* h)
{
    http_conf_destroy(h->handler.conf);
    free(h->handler.conf);
    free(h);
}
