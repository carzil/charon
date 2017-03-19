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

struct worker_s;

struct connection_s {
    struct list_node node;

    int fd;
    struct sockaddr addr;

    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];

    unsigned eof:1;

    chain_t chain;

    void* context;
    void (*on_request)(struct worker_s* s, struct connection_s* c);
    int (*on_event)(struct worker_s* s, struct connection_s* c, event_t* ev);

    event_t* timeout_event;
};

typedef struct connection_s connection_t;

connection_t* conn_create();
int conn_write(connection_t* c, chain_t* chain);
void conn_destroy(connection_t* c);
int conn_read(connection_t* c, buffer_t* buf);

#endif
