#ifndef _CLIENT_H_
#define _CLIENT_H_

#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "utils/list.h"
#include "http.h"
#include "chain.h"
#include "event.h"
#include "handler.h"

struct worker_s;

struct connection_s {
    struct list_node node;

    int fd;
    struct sockaddr_storage addr;

    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];

    unsigned eof:1;
    unsigned in_epoll:1;
    int epoll_flags;

    chain_t chain;

    handler_t* handler;
    struct worker_s* worker;

    event_t timeout_ev;
    event_t read_ev;
    event_t write_ev;
};

typedef struct connection_s connection_t;

int conn_init(connection_t* c, struct worker_s* w, handler_t* h, int fd);
int conn_write(connection_t* c, chain_t* chain);
void conn_destroy(connection_t* c);
size_t conn_read_max(connection_t* c, buffer_t* buf, size_t max);
int conn_connect(connection_t* c, struct addrinfo* addrinfo);

#define conn_read(c, buf) (conn_read_max(c, buf, buffer_size(buf)))

static inline void event_set_connection(event_t* ev, connection_t* c)
{
    ev->data = c;
    ev->fd = c->fd;
}

#define event_connection(ev) ((connection_t*) (ev)->data)

#endif
