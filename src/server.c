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
#include "utils/logging.h"
#include "utils/string.h"

/* TODO: now it's possible to perform a DoS attack on Charon:
 * hacker can open tons of HTTP-connections and Charon will not
 * close them. Need to create a mechanism to close
 * timed out connections.
 */

charon_server* create_server() {
    charon_server* server = (charon_server*) malloc(sizeof(charon_server));
    memset(server, '\0', sizeof(charon_server));
    server->socket = -1;
    server->is_running = 0;
    return server;
}

void destroy_server(charon_server* server) {
    free(server);
}

int set_fd_non_blocking(int fd) {
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

int start_server(charon_server* server, int port) {
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

connection_t* server_client_create(charon_server* server) {
    connection_t* client = charon_client_create();
    client->node = LIST_NODE_EMPTY;
    list_append(&server->clients, &client->node);
    return client;
}

void server_loop(charon_server* server) {
    charon_info("started server loop");
    struct epoll_event ctl_event;
    struct epoll_event events[MAX_EVENTS];
    while (server->is_running) {
        size_t events_count = epoll_wait(server->epoll_fd, events, MAX_EVENTS, -1);
        charon_debug("%d events in queue", events_count);
        for (size_t i = 0; i < events_count; i++) {
            struct epoll_event* event = events;
            if (event->data.fd == server->socket) {
                connection_t* client = server_client_create(server);
                socklen_t in_addr_len = sizeof(struct sockaddr);
                int new_fd = accept(server->socket, &client->addr, &in_addr_len);
                if (new_fd == -1) {
                    charon_perror("accept: ");
                    continue;
                }
                if (set_fd_non_blocking(new_fd) == -1) {
                    charon_perror("cannot set fd to non-blocking mode: ");
                    break;
                }

                memset(&ctl_event, 0, sizeof(ctl_event));
                client->fd = new_fd;
                ctl_event.data.ptr = client;
                ctl_event.events = EPOLLIN;

                if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, new_fd, &ctl_event) == -1) {
                    charon_perror("epoll_ctl: ");
                }

                char hbuf[NI_MAXHOST];
                char sbuf[NI_MAXSERV];
                if (getnameinfo(&client->addr, in_addr_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                    charon_info("accepted client from %s:%s (fd=%d)", hbuf, sbuf, new_fd);
                }
            } else {
                connection_t* client = (connection_t*) event->data.ptr;
                bool connection_ended = false;
                while (1) {
                    char buffer[1024];
                    int count = read(client->fd, buffer, sizeof(buffer));
                    if (count == -1) {
                        if (errno != EAGAIN) {
                            charon_perror("read: ");
                            connection_ended = true;
                        }
                        break;
                    } else if (count == 0) {
                        connection_ended = true;
                        break;
                    } else {
                        charon_debug("readed %d bytes from fd=%d", count, client->fd);
                        int res = http_parser_feed(&client->parser, buffer, count);
                        if (res < 0) {
                            shutdown(client->fd, SHUT_RDWR);
                            close(client->fd);
                        } else if (res == HTTP_PARSER_DONE_REQUEST) {
                            struct list_node* ptr = list_head(&client->parser.request_queue);
                            while (ptr && list_data(ptr, http_request_t, node)->parsed) {
                                http_request_t* request = list_peek(&client->parser.request_queue, http_request_t, node);
                                ptr = list_head(&client->parser.request_queue);
                                char rbuf[1000];
                                chain_t* ch = chain_create();
                                struct buffer* buf = buffer_create();
                                struct stat st;
                                char* path_z = copy_string_z(request->uri.path.start, string_size(&request->uri.path));
                                while (*path_z == '/') {
                                    path_z++;
                                }
                                charon_debug("open('%s')", path_z);
                                stat(path_z, &st);
                                int file_fd = open(path_z, O_RDONLY);
                                if (file_fd > 0) {
                                    snprintf(rbuf, 1000, "HTTP/1.1 200 Ok\r\nConnection: close\r\nContent-Length: %ld\r\n\r\n", st.st_size);
                                } else {
                                    charon_perror("open: ");
                                    snprintf(rbuf, 1000, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 44\r\n\r\n<html><body><h1>Not Found</h1></body></html>");
                                }
                                buf->memory.start = rbuf;
                                buf->size = strlen(rbuf);
                                buf->flags |= BUFFER_IN_MEMORY;
                                chain_push_buffer(ch, buf);
                                if (file_fd > 0) {
                                    buf = buffer_create();
                                    buf->file.fd = file_fd;
                                    buf->file.pos = 0;
                                    buf->size = st.st_size;
                                    buf->flags |= BUFFER_IN_FILE;
                                    chain_push_buffer(ch, buf);
                                }
                                connection_chain_write(client, request, ch);
                                charon_debug("found parsed request");
                            }
                        }
                    }
                }

                if (connection_ended) {
                    charon_info("client from %d disconnected", client->fd);
                    close(client->fd);
                }
            }
        }
    }
    // TODO: graceful shutdown
    shutdown(server->socket, SHUT_RDWR);
    close(server->socket);
}

void stop_server(charon_server* server) {
    server->is_running = false;
}

void sigint_handler(int sig) {
    charon_debug("Ctrl+C catched");
    stop_server(global_server);
    exit(0);
}

charon_server* global_server;

int server_main(int argc, char *argv[]) {
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
