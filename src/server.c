#include <arpa/inet.h>
#include <error.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <unistd.h>
#include <sys/stat.h>

#include "server.h"
#include "client.h"
#include "http_handler.h"
#include "utils/logging.h"
#include "utils/string.h"

/* TODO: now it's possible to perform a DoS attack on Charon:
 * hacker can open tons of HTTP-connections and Charon will not
 * close them. Need to create a mechanism to close
 * timed out connections.
 */

charon_server* create_server()
{
    charon_server* server = (charon_server*) malloc(sizeof(charon_server));
    memset(server, '\0', sizeof(charon_server));
    server->socket = -1;
    server->is_running = 0;
    return server;
}

void destroy_server(charon_server* server)
{
    free(server);
}

int set_fd_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        return -1;
    }

    return 0;
}

int start_server(charon_server* server, int port)
{
    struct addrinfo hints;
    struct addrinfo* result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char _port[5];
    snprintf(_port, 5, "%d", port);

    int res = getaddrinfo(NULL, _port, &hints, &result);

    if (res != 0) {
        charon_error("getaddrinfo failed: %s", gai_strerror(res));
        return 1;
    }

    for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
        server->socket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (server->socket != -1) {
            res = bind(server->socket, rp->ai_addr, rp->ai_addrlen);
            if (res == 0) {
                charon_info("charon running on %s:%d", inet_ntoa(server->addr.sin_addr), port);
                break;
            } else {
                close(server->socket);
            }
        }
    }

    freeaddrinfo(result);

    if (server->socket < 0) {
        charon_perror("socket: ");
        return 2;
    }

    if (res < 0) {
        charon_perror("bind: ");
        return 2;
    }

    int enable = 1;
    if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        charon_perror("setsockopt: ");
    }

    if (set_fd_non_blocking(server->socket) == -1) {
        charon_perror("cannot set socket to non-blocking mode: ");
        return 3;
    }

    if (listen(server->socket, SOMAXCONN) == -1) {
        charon_perror("listen: ");
    }   

    // epoll preparation
    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd == -1) {
        charon_perror("epoll_create: ");
        return 4;
    }

    struct epoll_event ctl_event;
    ctl_event.data.fd = server->socket;
    ctl_event.events = EPOLLIN | EPOLLET;

    res = epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->socket, &ctl_event);
    if (res == -1) {
        charon_perror("epoll_ctl: ");
        return 4;
    }

    server->is_running = true;

    return 0;
}

connection_t* server_client_create(charon_server* server)
{
    connection_t* client = chrn_conn_create();
    client->node = LIST_NODE_EMPTY;
    list_append(&server->clients, &client->node);
    return client;
}

int chrn_server_accept_client(charon_server* server, connection_t** result)
{
    struct epoll_event ctl_event;
    connection_t* client = server_client_create(server);
    socklen_t in_addr_len = sizeof(struct sockaddr);
    int error;

    int new_fd = accept(server->socket, &client->addr, &in_addr_len);
    if (new_fd == -1) {
        charon_perror("accept: ");
        error = -CHARON_ERR;
        goto cleanup;
    }
    if (set_fd_non_blocking(new_fd) == -1) {
        charon_perror("cannot set fd to non-blocking mode: ");
        error = -CHARON_ERR;
        goto cleanup;
    }

    memset(&ctl_event, 0, sizeof(ctl_event));
    client->fd = new_fd;
    ctl_event.data.ptr = client;
    ctl_event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, new_fd, &ctl_event) == -1) {
        charon_perror("epoll_ctl: ");
        error = -CHARON_ERR;
        goto cleanup;
    }

    if (getnameinfo(&client->addr, in_addr_len,
                    client->hbuf, sizeof(client->hbuf),
                    client->sbuf, sizeof(client->sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        charon_info("accepted client from %s:%s (fd=%d)", client->hbuf, client->sbuf, new_fd);
    }

    *result = client;
    return CHARON_OK;

    cleanup:
    chrn_conn_destroy(client);
    free(client);
    return error;
}

void charon_server_end_conn(charon_server* server, connection_t* c)
{
    charon_info("connection closed fd=%d, addr=%s:%s", c->fd, c->hbuf, c->sbuf);
    http_handler_on_connection_end(server, c);
    close(c->fd);
    list_remove(&server->clients, &c->node);
    chrn_conn_destroy(c);
    free(c);
}

void server_loop(charon_server* server)
{
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event* event;
    struct list_node* ptr;
    struct list_node* ptr_safe;
    connection_t* c;
    int events_count;

    charon_info("started server loop");
    while (server->is_running) {
        events_count = epoll_wait(server->epoll_fd, events, MAX_EVENTS, -1);

        if (events_count == -1) {
            charon_perror("epoll_wait: ");
            server->is_running = false;
            break;
        }

        charon_debug("%d events in queue", events_count);
        for (int i = 0; i < events_count; i++) {
            event = events + i;
            if (event->data.fd == server->socket) {
                chrn_server_accept_client(server, &c);
                http_handler_connection_init(c);
            } else {
                c = (connection_t*) event->data.ptr;
                c->on_request(server, c);
            }
        }
    }

    list_foreach_safe(&server->clients, ptr, ptr_safe) {
        c = list_data(ptr, connection_t, node);
        close(c->fd);
        chrn_conn_destroy(c);
    }

    charon_info("clients stopped");
    http_handler_on_finish(server);
    close(server->socket);
    charon_info("server stopped");
}

void stop_server(charon_server* server)
{
    server->is_running = false;
}

void sigint_handler(int sig)
{
    charon_debug("Ctrl+C catched");
    stop_server(global_server);
}

charon_server* global_server;

int server_main(int argc, char *argv[])
{
    if (argc < 1) {
        charon_error("no port provided");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    global_server = create_server();
    if (start_server(global_server, atoi(argv[0])) == 0) {
        server_loop(global_server);
    } else {
        charon_error("cannot start charon server");
    }

    destroy_server(global_server);

    return 0;
}
