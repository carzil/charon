#include <sys/sendfile.h>

#include "client.h"
#include "server.h"
#include "chain.h"
#include "utils/array.h"

connection_t* conn_create()
{
    connection_t* c = malloc(sizeof(connection_t));
    chain_init(&c->chain);
    return c;
}

void conn_destroy(connection_t* c)
{
    chain_destroy(&c->chain);
}

int conn_write(connection_t* client, chain_t* chain)
{
    off_t offset;

    while (!list_empty(&chain->buffers)) {
        buffer_t* buf = list_first_entry(chain->buffers, buffer_t, node);
        if (buf->in_memory) {
            int count = write(client->fd, buf->start + buf->pos, buf->size);
            if (count < 0) {
                switch (errno) {
                case EAGAIN:
                    return CHARON_AGAIN;
                default:
                    charon_perror("write: ");
                    return -CHARON_ERR;
                }
            } else {
                buf->pos += count;
                charon_debug("write to client fd=%d, count=%d", client->fd, count);
            }
        } else if (buf->in_file) {
            offset = buf->pos;
            int res = sendfile(client->fd, buf->fd, &offset, buf->size);
            charon_debug("sendfile to client fd=%d, from fd=%d, count=%jd", client->fd, buf->fd, (intmax_t)offset);
            if (res < 0) {
                charon_perror("sendfile: ");
                return -CHARON_ERR;
            }
            buf->pos += offset;
        }

        if (buf->pos == buf->size) {
            charon_debug("buffer done, client fd=%d", client->fd);
            list_remove(&buf->node);
            buffer_destroy(buf);
            free(buf);
        }
    }
    chain_destroy(&client->chain);
    charon_debug("all buffers done, client fd=%d", client->fd);
    return CHARON_OK;
}

int conn_read(connection_t* c, buffer_t* buf)
{
    int sum = 0, count;
    size_t avail_space;
    for (;;) {
        avail_space = buf->end - buf->last;
        if (!avail_space) {
            // still have data on socket, but buffer is full
            return -CHARON_BUFFER_FULL;
        }
        count = read(c->fd, buf->last, buf->end - buf->last);
        if (count == -1) {
            if (errno == EAGAIN) {
                // read all data on socket
                return sum;
            } else {
                charon_perror("read: ");
                return -CHARON_ERR;
            }
        } else if (count == 0) {
            // read return 0, it is end-of-stream
            c->eof = 1;
            return sum;
        } else {
            sum += count;
            buf->last += count;
        }
    }
}
