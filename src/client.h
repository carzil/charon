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
    struct http_parser parser;

    struct chain chain;
} charon_client_t;

charon_client_t* charon_client_create();

#endif