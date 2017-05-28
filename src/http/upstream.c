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
    chain_init(&uc->chain_in);
    chain_init(&uc->chain_out);
    buffer_init(&uc->recv_buf);
    buffer_malloc(&uc->recv_buf, 4096);
    http_response_init(&uc->resp);
    return uc;
}

void http_upstream_connection_destroy(http_upstream_connection_t* uc)
{
    if (uc->upstreaming != NULL) {
        http_upstream_break_off(uc->upstreaming);
    }
    chain_destroy(&uc->chain_in);
    chain_destroy(&uc->chain_out);
    buffer_destroy(&uc->recv_buf);
    http_parser_destroy(&uc->parser);
    http_response_destroy(&uc->resp);
    free(uc->conn.handler);
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

    while ((count = conn_read(&uc->conn, &uc->recv_buf)) == -CHARON_BUFFER_FULL) {
        uc->body_read += count;
    }

    return CHARON_OK;
}

int http_upstream_on_read_body(event_t* ev)
{
    ssize_t count;
    http_upstream_connection_t* uc = ev->data;

    if (uc->discard_response) {
        uc->conn.read_ev.handler = http_upstream_discard_body;
        return http_upstream_discard_body(ev);
    }

    /* FIXME: if upstream can send another data after body */
    while ((count = conn_read(&uc->conn, &uc->recv_buf)) == -CHARON_BUFFER_FULL) {
        buffer_t* copy = buffer_deep_copy(&uc->recv_buf);
        buffer_update_size(copy);
        chain_push_buffer(&uc->chain_out, copy);
        uc->body_read += buffer_size(&uc->recv_buf);
        uc->recv_buf.pos = buffer_size(&uc->recv_buf);
        buffer_rewind(&uc->recv_buf);
        charon_debug("%zu", buffer_size_last(&uc->recv_buf));
    }

    if (count < 0) {
        worker_disable_read(&uc->conn);
        worker_stop_connection(&uc->conn);
        return http_end_process_request(uc->upstreaming, HTTP_BAD_GATEWAY);
    }

    uc->body_read += count;

    charon_debug("%zu/%zu", uc->body_read, uc->resp.content_length);

    if (!http_version_equal(uc->resp.version, HTTP_VERSION_10)) {
        if (uc->body_read >= uc->resp.content_length) {
            buffer_t* copy = buffer_deep_copy(&uc->recv_buf);
            buffer_rewind(&uc->recv_buf);
            buffer_update_size(copy);
            chain_push_buffer(&uc->chain_out, copy);
            /* despite we read whole response from upstream,
             * we cannot enable read on upstreaming connection, because
             * new request can be "mixed" with old one */
            uc->response_received = 1;
        }
    }

    if (uc->conn.eof) {
        uc->response_received = 1;
    }

    if (uc->response_received) {
        return http_end_process_request(uc->upstreaming, 0);
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
            if (uc->conn.eof) {
                worker_stop_connection(&uc->conn);
            }
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
        http_end_process_request(uc->upstreaming, HTTP_BAD_GATEWAY);
        return CHARON_OK;
    } else if (count == 0 && uc->conn.eof) {
        charon_debug("%p %p", uc->recv_buf.last, uc->recv_buf.start + uc->recv_buf.pos);
        /* upstream closed connection */
        /* FIXME: need to check if request is formed */
        charon_debug("== [proxy response dump] ==\n%.*s==========================",
            (int)(uc->recv_buf.last - uc->recv_buf.start),
            uc->recv_buf.start
        );
        worker_stop_connection(&uc->conn);
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
        uc->body_read = buffer_size_last(&uc->recv_buf);
        uc->conn.read_ev.handler = http_upstream_on_read_body;
        uc->upstreaming->conn.write_ev.handler = http_upstream_on_write_to_client;
        return http_upstream_on_read_body(ev);
    } else {
        return http_end_process_request(hc, HTTP_BAD_GATEWAY);
    }

    return CHARON_OK;

err:
    if (res < 0) {
        http_end_process_request(uc->upstreaming, HTTP_BAD_GATEWAY);
        worker_stop_connection(&uc->conn);
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
    // connection_t* result = NULL;

    // if (list_empty(&upstream->idle_connections)) {
    //     result = http_upstream_make_connection(upstream);
    //     list_insert_first(&upstream->connections, &result->node);
    // } else {
    //     result = list_first_entry(&upstream->idle_connections, connection_t, node);
    // }

    return http_upstream_make_connection(upstream);
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

    write_header_s(string("Host"), req->host, buf);

    *buf->last++ = '\r';
    *buf->last++ = '\n';

    charon_debug("== [proxy request dump] ==\n%.*s==========================",
        (int)(buffer_size_last(buf)), buf->start
    );

    chain_push_buffer(&uc->chain_in, buf);
    buffer_update_size(buf);
    worker_enable_write(&uc->conn);

    return CHARON_OK;
}

int http_upstream_bond(http_upstream_connection_t* uc, http_connection_t* hc)
{
    // TODO: clean current buffer
    hc->upstream_conn = uc;
    uc->upstreaming = hc;
    uc->discard_response = 0;
    uc->body_read = 0;
    uc->conn.worker = hc->conn.worker;
    hc->conn.write_ev.handler = http_upstream_on_write_to_client;
    worker_add_connection(hc->conn.worker, &uc->conn);
    http_parser_init(&uc->parser, HTTP_PARSE_RESPONSE);
    http_response_init(&uc->resp);
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
