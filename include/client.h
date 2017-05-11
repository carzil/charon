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

    void* context;
    handler_t* handler;
    struct worker_s* worker;

    event_t timeout_ev;
    event_t read_ev;
    event_t write_ev;
};

typedef struct connection_s connection_t;

connection_t* conn_create();
int conn_write(connection_t* c, chain_t* chain);
void conn_destroy(connection_t* c);
int conn_read(connection_t* c, buffer_t* buf);

static inline void event_set_connection(event_t* ev, connection_t* c)
{
    ev->data = c;
    ev->fd = c->fd;
}

#define event_connection(ev) ((connection_t*) &(ev)->data)

#endif
