#include <sys/sendfile.h>

#include "client.h"
#include "server.h"
#include "utils/array.h"

connection_t* charon_client_create() {
    connection_t* client = (connection_t*) malloc(sizeof(connection_t));
    http_parser_init(&client->parser);
    return client;
}

int connection_chain_write(connection_t* client, http_request_t* r, chain_t* chain) {
    struct list_node* ptr;
    off_t offset;
    list_foreach(&chain->buffers, ptr) {
        struct buffer* buf = list_data(ptr, struct buffer, node);
        if (buf->flags & BUFFER_IN_MEMORY) {
            int count = write(client->fd, buf->memory.start, buf->size);
            charon_debug("write to client fd=%d, count=%d", client->fd, count);
        } else if (buf->flags & BUFFER_IN_FILE) {
            offset = buf->file.pos;
            charon_debug("sendfile to client fd=%d, from fd=%d, count=%d", client->fd, buf->file.fd, offset);
            sendfile(client->fd, buf->file.fd, &offset, buf->size);
            if (offset < 0) {
                charon_perror("sendfile: ");
            }
        }
    }
    return CHARON_OK;
}
