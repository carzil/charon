#include <limits.h>

#include "http/upstream.h"
#include "worker.h"

http_upstream_connection_t* http_upstream_connection_create()
{
    http_upstream_connection_t* uc = malloc(sizeof(http_upstream_connection_t));
    uc->status_line_parsed = 0;
    uc->headers_parsed = 0;
    uc->response_received = 0;
    uc->resp.content_length = SIZE_MAX;
    uc->upstreaming = NULL;
    uc->used = 0;
    chain_init(&uc->chain_in);
    chain_init(&uc->chain_out);
    buffer_init(&uc->recv_buf);
    buffer_malloc(&uc->recv_buf, 4096);
    http_response_init(&uc->resp);
    return uc;
}

void http_upstream_connection_cleanup(http_upstream_connection_t* uc)
{
    uc->status_line_parsed = 0;
    uc->headers_parsed = 0;
    uc->response_received = 0;
    uc->resp.content_length = SIZE_MAX;
    uc->upstreaming = NULL;
    uc->used = 0;
    http_body_cleanup(&uc->body);
    http_parser_init(&uc->parser, HTTP_PARSE_RESPONSE);
    http_response_clean(&uc->resp);
    buffer_clean(&uc->recv_buf);
}

void http_upstream_connection_destroy(http_upstream_connection_t* uc)
{
    // if (!uc->used) {
    //     list_remove(&uc->node);
    // }
    if (uc->upstreaming != NULL) {
        http_upstream_break_off(uc->upstreaming);
    }
    chain_destroy(&uc->chain_in);
    chain_destroy(&uc->chain_out);
    buffer_destroy(&uc->recv_buf);
    http_parser_destroy(&uc->parser);
    http_response_destroy(&uc->resp);
    charon_debug("d %p", &uc->resp);
    free(uc->conn.handler);
}

void http_upstream_init(http_upstream_t* u)
{
    list_head_init(u->idle_connections);
}

void http_upstream_destroy(http_upstream_t* u)
{
    free(u->uri.start);
}

static void http_upstream_handle_header(http_upstream_connection_t* uc, http_header_t* header)
{
    if (!string_cmpl(&header->name, "Content-Length")) {
        uc->resp.content_length = strtoumax(header->value.start, NULL, 0);
    } else {
        vector_push(&uc->resp.headers, header, http_header_t);
    }
}

int http_upstream_discard_body(event_t* ev)
{
    ssize_t count;
    http_upstream_connection_t* uc = ev->data;
    /* TODO: move to body.c */

    while ((count = conn_read(&uc->conn, &uc->recv_buf)) == -CHARON_BUFFER_FULL) {
    }

    return CHARON_OK;
}

void http_upstream_connection_put(http_upstream_connection_t* uc)
{
    uc->used = 0;
    http_upstream_connection_cleanup(uc);
    list_insert_first(&uc->upstream->idle_connections, &uc->node);
    charon_debug("put %p", uc);
}

void http_upstream_connection_stop(http_upstream_connection_t* uc)
{
    http_upstream_break_off(uc->upstreaming);
    worker_stop_connection(&uc->conn);
}

int http_upstream_dummy_read(event_t* ev)
{
    ssize_t count;
    http_upstream_connection_t* uc = ev->data;

    count = conn_read(&uc->conn, &uc->recv_buf);

    if (count == 0 && uc->conn.eof) {
        charon_debug("upstream closed connection fd=%d", uc->conn.fd);
    } else {
        charon_debug("unexpected data from upstream fd=%d", uc->conn.fd);
        http_upstream_connection_stop(uc);
    }

    return CHARON_OK;
}

int http_upstream_body_done(http_upstream_connection_t* uc)
{
    charon_debug("read whole body from fd=%d", uc->conn.fd);
    uc->response_received = 1;
    uc->conn.read_ev.handler = http_upstream_dummy_read;
    worker_enable_read(&uc->conn);
    return http_end_process_request(uc->upstreaming, 0);
}

int http_upstream_on_read_body(event_t* ev)
{
    int res;
    http_upstream_connection_t* uc = ev->data;
    connection_t* c = &uc->conn;

    if (uc->discard_response) {
        uc->conn.read_ev.handler = http_upstream_discard_body;
        return http_upstream_discard_body(ev);
    }

    res = http_body_read(c, &uc->body);

    if (res == CHARON_OK) {
        return http_upstream_body_done(uc);
    } else if (res < 0) {
        http_upstream_connection_stop(uc);
        return http_end_process_request(uc->upstreaming, HTTP_BAD_GATEWAY);
    }

    return CHARON_OK;
}

int http_upstream_on_write_to_client(event_t* ev)
{
    connection_t* c = ev->data;
    http_connection_t* hc = http_connection(c);
    http_upstream_connection_t* uc = hc->upstream_conn;

    int res = conn_write(c, &uc->chain_out);
    if (res == CHARON_OK) {
        worker_disable_write(c);
        chain_clear(&uc->chain_out);
        if (uc->response_received) {
            http_upstream_connection_stop(uc);
            return http_prepare_for_next_request(hc);
        }
    }

    return CHARON_OK;
}

void http_upstream_process_headers(http_upstream_connection_t* uc)
{
    for (size_t i = 0; i < vector_size(&uc->resp.headers); i++) {
        if (!string_cmpl(&uc->resp.headers[i].name, "Server")) {
            uc->resp.headers[i].value = string("charon/v" CHARON_VERSION);
        }
    }
}

static buffer_t* make_head(http_upstream_connection_t* uc)
{
    buffer_t* buf = buffer_create();
    buffer_malloc(buf, 4096);
    uc->resp.version.major = 1;
    uc->resp.version.minor = 1;
    http_write_status_line(buf, uc->resp.version, uc->resp.status, uc->resp.status_message);
    for (size_t i = 0; i < vector_size(&uc->resp.headers); i++) {
        write_header_s(uc->resp.headers[i].name, uc->resp.headers[i].value, buf);
    }
    write_header_i(string("Content-Length"), uc->resp.content_length, buf);
    *buf->last++ = '\r';
    *buf->last++ = '\n';
    buffer_update_size(buf);
    return buf;
}

int http_upstream_on_read(event_t* ev)
{
    int count, res = -1;
    http_upstream_connection_t* uc = ev->data;
    http_connection_t* hc = uc->upstreaming;

    count = conn_read(&uc->conn, &uc->recv_buf);

    if (count < 0 && count != -CHARON_BUFFER_FULL) {
        worker_disable_read(&uc->conn);
        http_upstream_connection_stop(uc);
        return http_end_process_request(hc, HTTP_BAD_GATEWAY);
    } else if (count == 0 && uc->conn.eof) {
        http_upstream_connection_stop(uc);
        return CHARON_OK;
    }

    if (!uc->status_line_parsed) {
        res = http_parse_status_line(&uc->parser, &uc->recv_buf, &uc->resp.status, &uc->resp.status_message, &uc->resp.version);
        if (res == HTTP_PARSER_DONE) {
            uc->status_line_parsed = 1;
        } else {
            goto err;
        }
    }

    while (!uc->headers_parsed) {
        res = http_parse_header(&uc->parser, &uc->recv_buf, &uc->header);
        if (res == HTTP_PARSER_DONE) {
            uc->headers_parsed = 1;
            http_upstream_process_headers(uc);
            break;
        } else if (res == HTTP_PARSER_OK) {
            http_upstream_handle_header(uc, &uc->header);
            charon_debug("parsed header name='%.*s', value='%.*s'",
                    (int)string_size(&uc->header.name), uc->header.name.start,
                    (int)string_size(&uc->header.value), uc->header.value.start
            );
        } else {
            goto err;
        }
    }

    if (uc->status_line_parsed && uc->headers_parsed) {
        buffer_t* header_buffer = make_head(uc);
        chain_push_buffer(&uc->chain_out, header_buffer);
        buffer_rewind(&uc->recv_buf);
        uc->upstreaming->conn.write_ev.handler = http_upstream_on_write_to_client;
        http_body_init(&uc->body, &uc->chain_out, uc->resp.content_length, 4096);
        http_body_preread(&uc->body, &uc->recv_buf, buffer_size_last(&uc->recv_buf));
        if (http_body_done(&uc->body)) {
            /* whole body is in header buffer */
            return http_upstream_body_done(uc);
        } else {
            uc->conn.read_ev.handler = http_upstream_on_read_body;
            return http_upstream_on_read_body(ev);
        }
    } else {
        return http_end_process_request(hc, HTTP_BAD_GATEWAY);
    }

    return CHARON_OK;

err:
    if (res < 0) {
        http_upstream_connection_stop(uc);
        return http_end_process_request(hc, HTTP_BAD_GATEWAY);
    }

    return CHARON_OK;
}

int http_upstream_on_write(event_t* ev)
{
    http_upstream_connection_t* uc = ev->data;

    int res = conn_write(&uc->conn, &uc->chain_in);
    if (res == CHARON_OK) {
        worker_disable_write(&uc->conn);
        chain_clear(&uc->chain_in);
        worker_enable_read(&uc->conn);
    }
    return CHARON_OK;
}

http_upstream_connection_t* http_upstream_make_connection(http_upstream_t* upstream)
{
    http_upstream_connection_t* uc = NULL;

    struct addrinfo* result;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_flags = 0,
        .ai_socktype = SOCK_STREAM
    };
    char* ch = upstream->uri.start;
    while (*ch) {
        ch++;
    }
    ch++;
    int res = getaddrinfo(upstream->uri.start, ch, &hints, &result);
    if (res) {
        charon_error("getaddrinfo failed: %s", gai_strerror(res));
        return NULL;
    }
    uc = http_upstream_connection_create();
    conn_init(&uc->conn, NULL, NULL, -1);
    if (conn_connect(&uc->conn, result) != CHARON_OK) {
        http_upstream_connection_destroy(uc);
        conn_destroy(&uc->conn);
        close(uc->conn.fd);
        free(uc);
        uc = NULL;
        goto cleanup;
    }
    charon_debug("upstream connected, fd=%d", uc->conn.fd);
    uc->upstream = upstream;
    event_set_connection(&uc->conn.read_ev, &uc->conn);
    event_set_connection(&uc->conn.write_ev, &uc->conn);
    event_set_connection(&uc->conn.timeout_ev, &uc->conn);
    uc->conn.read_ev.handler = http_upstream_on_read;
    uc->conn.write_ev.handler = http_upstream_on_write;
    uc->conn.timeout_ev.handler = NULL;
    uc->conn.handler = malloc(sizeof(handler_t));
    uc->conn.handler->destroy_connection = (connection_destroyer_t) http_upstream_connection_destroy;

cleanup:
    freeaddrinfo(result);
    return uc;
}

http_upstream_connection_t* http_upstream_connect(http_upstream_t* upstream)
{
    http_upstream_connection_t* result = http_upstream_make_connection(upstream);;

    // if (list_empty(&upstream->idle_connections)) {
    //     result = http_upstream_make_connection(upstream);
    //     charon_debug("new connection at %p", result);
    // } else {
    //     result = list_first_entry(upstream->idle_connections, http_upstream_connection_t, node);
    //     charon_debug("reusing connection fd=%d, %p", 0, result);
    // }

    result->used = 1;

    return result;
}

int http_upstream_proxy_request(http_upstream_connection_t* uc)
{
    http_connection_t* hc = uc->upstreaming;
    http_request_t* req = &hc->request;

    buffer_t* buf = buffer_create();
    buffer_malloc(buf, 4096);

    const char* method_name;

    /* TODO: move to http.c */
    switch (req->method) {
    case HTTP_GET:
        method_name = "GET";
        break;
    case HTTP_POST:
        method_name = "POST";
        break;
    default:
        return -CHARON_ERR;
    }

    buf->last += snprintf(buf->start, 4096, "%s %.*s HTTP/%d.%d\r\n",
        method_name,
        (int)(string_size(&req->uri.path)), req->uri.path.start,
        req->version.major,
        req->version.minor
    );

    for (size_t i = 0; i < vector_size(&req->headers); i++) {
        write_header_s(req->headers[i].name, req->headers[i].value, buf);
    }

    write_header_s(string("Connection"), string("close"), buf);
    write_header_s(string("Host"), req->host, buf);

    if (req->content_length > 0) {
        write_header_i(string("Content-Length"), req->content_length, buf);
    }

    *buf->last++ = '\r';
    *buf->last++ = '\n';

    charon_debug("== [proxy request dump] ==\n%.*s==========================",
        (int)(buffer_size_last(buf)), buf->start
    );

    chain_push_buffer(&uc->chain_in, buf);
    chain_link_chain(&uc->chain_in, &hc->body_chain);
    buffer_update_size(buf);
    worker_enable_write(&uc->conn);

    return CHARON_OK;
}

int http_upstream_bond(http_upstream_connection_t* uc, http_connection_t* hc)
{
    // TODO: clean current buffer
    http_upstream_connection_cleanup(uc);
    hc->upstream_conn = uc;
    uc->upstreaming = hc;
    uc->discard_response = 0;
    uc->conn.worker = hc->conn.worker;
    hc->conn.read_ev.handler = http_upstream_on_read;
    hc->conn.write_ev.handler = http_upstream_on_write_to_client;
    worker_add_connection(hc->conn.worker, &uc->conn);
    return http_upstream_proxy_request(uc);
}

void http_upstream_break_off(http_connection_t* hc)
{
    http_upstream_connection_t* uc = hc->upstream_conn;
    hc->upstream_conn = NULL;
    if (uc != NULL) {
        uc->discard_response = 1;
        uc->upstreaming = NULL;
    }
}
