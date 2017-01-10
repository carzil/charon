#ifndef _CHARON_SERVER_H_
#define _CHARON_SERVER_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/epoll.h>

#include "http_parser.h"
#include "utils/list.h"
#include "client.h"

#define MAX_EVENTS 64
#define INITIAL_CLIENT_BUFFER_SIZE 128

struct charon_server_s {
    int socket;
    struct sockaddr_in addr;
    int epoll_fd;
    bool is_running;

    struct list clients;
};

typedef struct charon_server_s charon_server;

int server_main(int argc, char *argv[]);

extern charon_server* global_server;

charon_server* create_server();
void destroy_server(charon_server* worker);
int start_server(charon_server* server, int port);
void stop_server(charon_server* server);
void server_loop(charon_server* server);
void charon_server_end_conn(charon_server* server, connection_t* c);

#endif
