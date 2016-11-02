#include <sys/sendfile.h>

#include "client.h"
#include "server.h"
#include "utils/array.h"

charon_client_t* charon_client_create() {
    charon_client_t* client = (charon_client_t*) malloc(sizeof(charon_client_t));
    http_parser_init(&client->parser);
    return client;
}

int client_chain_write(charon_client_t* client, struct http_request* r, struct chain* chain) {
    struct list_node* ptr;
    off_t offset;
    list_foreach(&chain->buffers, ptr) {
        struct buffer* buf = list_data(ptr, struct buffer);
        charon_debug("%d", buf->flags);
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
