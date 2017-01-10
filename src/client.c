#include <sys/sendfile.h>

#include "client.h"
#include "server.h"
#include "chain.h"
#include "utils/array.h"

connection_t* chrn_conn_create()
{
    connection_t* c = malloc(sizeof(connection_t));
    chain_init(&c->chain);
    return c;
}

void chrn_conn_destroy(connection_t* c)
{
    chain_destroy(&c->chain);
}

int chrn_conn_write(connection_t* client, chain_t* chain)
{
    struct list_node* ptr;
    off_t offset;
    list_foreach(&chain->buffers, ptr) {
        buffer_t* buf = list_data(ptr, buffer_t, node);
        if (buf->in_memory) {
            int count = write(client->fd, buf->start, buf->size);
            if (count < 0) {
                charon_perror("write: ");
            } else {
                charon_debug("write to client fd=%d, count=%d", client->fd, count);
            }
        } else if (buf->in_file) {
            offset = buf->pos;
            sendfile(client->fd, buf->fd, &offset, buf->size);
            charon_debug("sendfile to client fd=%d, from fd=%d, count=%d", client->fd, buf->fd, offset);
            if (offset < 0) {
                charon_perror("sendfile: ");
            }
        }
    }
    return CHARON_OK;
}

int charon_conn_read(connection_t* c, buffer_t* buf)
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