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
#include "handlers/http_handler.h"
#include "utils/logging.h"
#include "utils/string.h"

/*
 * TODO: now it's possible to perform a DoS attack on Charon:
 * hacker can open tons of HTTP-connections and Charon will not
 * close them. Need to create a mechanism to close
 * timed out connections.
 */

worker_t* worker_create(config_t* conf)
{
    worker_t* worker = (worker_t*) malloc(sizeof(worker_t));
    memset(worker, '\0', sizeof(worker_t));
    worker->socket = -1;
    worker->is_running = 0;
    worker->conf = conf;
    LIST_HEAD_INIT(worker->clients);
    timer_queue_init(&worker->timer_queue);
    return worker;
}

void worker_destroy(worker_t* worker)
{
    timer_queue_destroy(&worker->timer_queue);
    free(worker);
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

int worker_start(worker_t* worker, int port)
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
        worker->socket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (worker->socket != -1) {
            res = bind(worker->socket, rp->ai_addr, rp->ai_addrlen);
            if (res == 0) {
                charon_info("charon running on %s:%d", inet_ntoa(worker->addr.sin_addr), port);
                break;
            } else {
                close(worker->socket);
            }
        }
    }

    freeaddrinfo(result);

    if (worker->socket < 0) {
        charon_perror("socket: ");
        return 2;
    }

    if (res < 0) {
        charon_perror("bind: ");
        return 2;
    }

    int enable = 1;
    if (setsockopt(worker->socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        charon_perror("setsockopt: ");
    }

    if (set_fd_non_blocking(worker->socket) == -1) {
        charon_perror("cannot set socket to non-blocking mode: ");
        return 3;
    }

    if (listen(worker->socket, SOMAXCONN) == -1) {
        charon_perror("listen: ");
    }

    // epoll preparation
    worker->epoll_fd = epoll_create1(0);
    if (worker->epoll_fd == -1) {
        charon_perror("epoll_create: ");
        return 4;
    }

    struct epoll_event ctl_event;
    ctl_event.data.fd = worker->socket;
    ctl_event.events = EPOLLIN | EPOLLET;

    res = epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, worker->socket, &ctl_event);
    if (res == -1) {
        charon_perror("epoll_ctl: ");
        return 4;
    }

    worker->is_running = true;

    return 0;
}

connection_t* worker_client_create(worker_t* worker)
{
    connection_t* client = conn_create();
    list_insert_last(&worker->clients, &client->node);
    return client;
}

int worker_accept_client(worker_t* worker, connection_t** result)
{
    struct epoll_event ctl_event;
    connection_t* client;
    socklen_t in_addr_len = sizeof(struct sockaddr);
    struct sockaddr addr;
    int error;

    int new_fd = accept(worker->socket, &addr, &in_addr_len);

    if (new_fd < 0) {
        switch (errno) {
        case EAGAIN:
            return -CHARON_AGAIN;
        default:
            charon_perror("accept: ");
            return -CHARON_ERR;
        }
    }

    if (set_fd_non_blocking(new_fd) < 0) {
        charon_perror("cannot set fd to non-blocking mode: ");
        return -CHARON_ERR;
    }

    client = worker_client_create(worker);
    memcpy(&client->addr, &addr, in_addr_len);

    client->fd = new_fd;
    ctl_event.data.ptr = client;
    ctl_event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, new_fd, &ctl_event) < 0) {
        charon_perror("epoll_ctl: ");
        error = -CHARON_ERR;
        goto cleanup;
    }

    if (!getnameinfo(&client->addr, in_addr_len,
                    client->hbuf, sizeof(client->hbuf),
                    client->sbuf, sizeof(client->sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV)) {
        charon_info("accepted client from %s:%s (fd=%d)", client->hbuf, client->sbuf, new_fd);
    }

    *result = client;
    return CHARON_OK;

cleanup:
    conn_destroy(client);
    free(client);
    return error;
}

void worker_schedule_write(worker_t* worker, connection_t* c)
{
    struct epoll_event ctl_event;
    memset(&ctl_event, 0, sizeof(ctl_event));
    ctl_event.data.ptr = c;
    ctl_event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, c->fd, &ctl_event) < 0) {
        charon_perror("epoll_ctl: ");
    }
    charon_debug("connection was scheduled for write (fd=%d)", c->fd);
}

void worker_unschedule_write(worker_t* worker, connection_t* c)
{
    struct epoll_event ctl_event;
    memset(&ctl_event, 0, sizeof(ctl_event));
    ctl_event.data.ptr = c;
    ctl_event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, c->fd, &ctl_event) < 0) {
        charon_perror("epoll_ctl: ");
    }
    charon_debug("connection was unscheduled for write (fd=%d)", c->fd);
}

void worker_stop_connection(worker_t* worker, connection_t* c)
{
    charon_info("connection closed fd=%d, addr=%s:%s", c->fd, c->hbuf, c->sbuf);
    http_handler_on_connection_end(worker, c);
    timer_queue_remove(&worker->timer_queue, c->timeout_event);
    event_destroy(c->timeout_event);
    free(c->timeout_event);
    list_remove(&c->node);
    close(c->fd);
    conn_destroy(c);
    free(c);
}

int worker_accept_connections(worker_t* worker)
{
    connection_t* c = NULL;
    int res, cnt = 0;
    while ((res = worker_accept_client(worker, &c)) == CHARON_OK) {
        http_handler_connection_init(worker, c);
        cnt++;
    }
    return cnt;
}

void worker_finish(worker_t* worker)
{
    struct list_node* ptr;
    struct list_node* ptr_safe;
    connection_t* c;

    list_foreach_safe(&worker->clients, ptr, ptr_safe) {
        c = list_entry(ptr, connection_t, node);
        close(c->fd);
        conn_destroy(c);
    }

    charon_info("clients stopped");
    http_handler_on_finish(worker);
    close(worker->socket);
    charon_info("worker stopped");
}

void worker_loop(worker_t* worker)
{
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event* epoll_event;
    connection_t* c;
    int events_count, res;

    charon_info("started worker loop");
    while (worker->is_running) {
        msec_t timeout = -1;
        if (!timer_queue_empty(&worker->timer_queue)) {
            timeout = timer_queue_top(&worker->timer_queue)->expire - get_current_msec();
        }
        events_count = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, (int)timeout);

        if (events_count < 0) {
            charon_perror("epoll_wait: ");
            worker->is_running = false;
            break;
        }

        charon_debug("%d events in queue", events_count);
        for (int i = 0; i < events_count; i++) {
            epoll_event = events + i;
            c = (connection_t*) epoll_event->data.ptr;
            if (epoll_event->data.fd == worker->socket) {
                worker_accept_connections(worker);
            } else {
                if (epoll_event->events & EPOLLIN) {
                    charon_debug("read event on fd=%d", c->fd);
                    c->on_request(worker, c);
                }

                if (epoll_event->events & EPOLLOUT) {
                    charon_debug("write event on fd=%d", c->fd);
                    res = conn_write(c, &c->chain);
                    if (res == CHARON_OK) {
                        worker_unschedule_write(worker, c);
                    }
                }
            }
        }

        msec_t c_time = get_current_msec();
        event_t* ev;
        charon_debug("processing timeouts");
        while (!timer_queue_empty(&worker->timer_queue) &&
            (ev = timer_queue_top(&worker->timer_queue))->expire <= c_time) {
            if (ev->conn->on_event != NULL) {
                charon_debug("event on connection fd=%d", ev->conn->fd);
                if (ev->conn->on_event(worker, ev->conn, ev)) {
                    timer_queue_remove(&worker->timer_queue, ev);
                }
            }
        }
        charon_debug("event loop end");
    }

    worker_finish(worker);
}

void worker_stop(worker_t* worker)
{
    worker->is_running = false;
}
