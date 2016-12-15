#ifndef _CLIENT_H_
#define _CLIENT_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/epoll.h>

#include "utils/list.h"
#include "http_parser.h"
#include "chain.h"

typedef struct {
    struct list_node node;

    int fd;
    struct sockaddr addr;
    http_parser_t parser;

    chain_t chain;
} connection_t;

connection_t* charon_client_create();
int connection_chain_write(connection_t* client, http_request_t* r, chain_t* chain);

#endif
