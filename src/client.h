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

struct charon_server_s;

struct connection_s {
    struct list_node node;

    int fd;
    struct sockaddr addr;

    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];

    int eof:1;

    chain_t chain;

    void* context;
    void (*on_request)(struct charon_server_s* s, struct connection_s* c);
};

typedef struct connection_s connection_t;

connection_t* chrn_conn_create();
int chrn_conn_write(connection_t* c, chain_t* chain);
void chrn_conn_destroy(connection_t* c);
int charon_conn_read(connection_t* c, buffer_t* buf);

#endif
