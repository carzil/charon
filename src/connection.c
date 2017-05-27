#include <sys/sendfile.h>

#include "connection.h"
#include "worker.h"
#include "chain.h"
#include "utils/array.h"

int conn_init(connection_t* c, worker_t* w, handler_t* h, int fd)
{
    c->in_epoll = 0;
    c->epoll_flags = 0;
    c->handler = h;
    c->worker = w;
    c->fd = fd;
    event_init(&c->read_ev);
    event_init(&c->write_ev);
    event_init(&c->timeout_ev);
    chain_init(&c->chain);
    return CHARON_OK;
}

void conn_destroy(connection_t* c)
{
    chain_destroy(&c->chain);
}

int conn_write_buf(connection_t* client, buffer_t* buf)
{
    off_t offset;

    if (buf->in_memory) {
        charon_debug("in-memory buffer, pos=%d, size=%zu", buf->pos, buf->size);
        while (buf->pos < buf->size) {
            ssize_t count = send(client->fd, buf->start + buf->pos, buf->size, MSG_NOSIGNAL);
            if (count < 0) {
                switch (errno) {
                case EAGAIN:
                    return -CHARON_AGAIN;
                default:
                    charon_perror("write: ");
                    return -CHARON_ERR;
                }
            }
            buf->pos += count;
            charon_debug("write to client fd=%d, count=%zd", client->fd, count);
        }
    } else if (buf->in_file) {
        while (buf->pos < buf->size) {
            offset = buf->pos;
            ssize_t res = sendfile(client->fd, buf->fd, &offset, buf->size);
            if (res < 0) {
                switch (errno) {
                case EAGAIN:
                    return -CHARON_AGAIN;
                default:
                    charon_perror("sendfile: ");
                    return -CHARON_ERR;
                }
            }
            buf->pos += offset;
            charon_debug("sendfile to client fd=%d, from fd=%d, count=%jd", client->fd, buf->fd, (intmax_t)offset);
        }
    }
    return CHARON_OK;
}

/* Writes chain to connection. If error occurs, returns error code */
int conn_write(connection_t* c, chain_t* chain)
{
    list_node_t* ptr;

    list_foreach(&chain->buffers, ptr) {
        buffer_t* buf = list_entry(ptr, buffer_t, node);
        ssize_t res = conn_write_buf(c, buf);
        if (res < 0) {
            return res;
        }
    }
    charon_debug("all buffers done, client fd=%d", c->fd);
    return CHARON_OK;
}

/*
 * This routine receives all available data from connection and puts it to buffer.
 * If buffer is full, -CHARON_BUFFER_FULL is returned.
 * Returns amount of read bytes. If EOF reached, c->eof is set.
 */
int conn_read(connection_t* c, buffer_t* buf)
{
    ssize_t sum = 0, count;
    size_t avail_space;
    for (;;) {
        avail_space = buf->end - buf->last;
        if (!avail_space) {
            return -CHARON_BUFFER_FULL;
        }
        count = recv(c->fd, buf->last, buf->end - buf->last, 0);
        if (count < 0) {
            if (errno == EAGAIN) {
                break;
            } else {
                charon_perror("read: ");
                return -CHARON_ERR;
            }
        } else if (count == 0) {
            /* read returned 0, it is EOF */
            c->eof = 1;
            break;
        } else {
            sum += count;
            buf->last += count;
        }
    }
    buffer_update_size(buf);
    return sum;
}

int conn_connect(connection_t* c, struct addrinfo* addrinfo)
{
    c->fd = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
    if (c->fd < 0) {
        charon_perror("socket: ");
        return -CHARON_ERR;
    }
    int res = connect(c->fd, addrinfo->ai_addr, addrinfo->ai_addrlen);
    if (res < 0) {
        charon_perror("connect: ");
        return -CHARON_ERR;
    }
    set_fd_non_blocking(c->fd);
    return CHARON_OK;
}
