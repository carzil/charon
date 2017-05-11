#define _GNU_SOURCE
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
#include "http/handler.h"
#include "utils/logging.h"
#include "utils/string.h"

worker_t* worker_create(char* conf_filename)
{
    worker_t* worker = (worker_t*) malloc(sizeof(worker_t));
    worker->socket = -1;
    worker->is_running = 0;
    worker->conf_filename = conf_filename;
    list_head_init(worker->clients);
    timer_queue_init(&worker->timer_queue);
    vector_init(&worker->connections);
    return worker;
}

void worker_destroy(worker_t* worker)
{
    timer_queue_destroy(&worker->timer_queue);
    vector_destroy(&worker->connections);
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
                charon_info("charon running on %s:%d", inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr), port);
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

int worker_accept_client(worker_t* worker, handler_t* handler, connection_t** result)
{
    connection_t* client;
    socklen_t in_addr_len = sizeof(struct sockaddr);
    struct sockaddr addr;

    int new_fd = accept4(worker->socket, &addr, &in_addr_len, O_NONBLOCK);

    if (new_fd < 0) {
        switch (errno) {
        case EAGAIN:
            return -CHARON_AGAIN;
        default:
            charon_perror("accept: ");
            return -CHARON_ERR;
        }
    }

    client = conn_create();
    client->fd = new_fd;
    client->handler = handler;
    client->worker = worker;
    list_insert_last(&worker->clients, &client->node);
    vector_set(&worker->connections, client->fd, &client, connection_t*);
    memcpy(&client->addr, &addr, in_addr_len);

    if (!getnameinfo((struct sockaddr*) &client->addr, in_addr_len,
                    client->hbuf, sizeof(client->hbuf),
                    client->sbuf, sizeof(client->sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV)) {
        charon_info("accepted client from %s:%s (fd=%d)", client->hbuf, client->sbuf, new_fd);
    }

    *result = client;

    return CHARON_OK;
}

void worker_epoll_event_ctl(worker_t* worker, connection_t* c, event_t* ev, int mode, int flags)
{
    struct epoll_event ctl_event;
    memset(&ctl_event, 0, sizeof(ctl_event));
    ctl_event.data.fd = c->fd;
    ctl_event.events = flags;
    if (epoll_ctl(worker->epoll_fd, mode, ev->fd, &ctl_event) < 0) {
        charon_perror("epoll_ctl: ");
    }
}

void worker_epoll_modify_or_add(worker_t* worker, connection_t* c, event_t* ev, int flags)
{
    if (c->in_epoll) {
        worker_epoll_event_ctl(worker, c, ev, EPOLL_CTL_MOD, flags);
    } else {
        worker_epoll_event_ctl(worker, c, ev, EPOLL_CTL_ADD, flags);
        c->in_epoll = 1;
    }
    c->epoll_flags = flags;
}

void worker_enable_read(connection_t* c)
{
    charon_debug("enable read fd=%d", c->fd);
    worker_epoll_modify_or_add(c->worker, c, &c->read_ev, c->epoll_flags | EPOLLIN | EPOLLET);
}

void worker_disable_read(connection_t* c)
{
    charon_debug("disable read fd=%d", c->fd);
    worker_epoll_modify_or_add(c->worker, c, &c->read_ev, (c->epoll_flags ^ EPOLLIN) | EPOLLET);
}

void worker_enable_write(connection_t* c)
{
    charon_debug("enable write fd=%d", c->fd);
    worker_epoll_modify_or_add(c->worker, c, &c->write_ev, c->epoll_flags | EPOLLOUT | EPOLLET);
}

void worker_disable_write(connection_t* c)
{
    charon_debug("disable write fd=%d", c->fd);
    worker_epoll_modify_or_add(c->worker, c, &c->write_ev, (c->epoll_flags ^ EPOLLOUT) | EPOLLET);
}

void worker_delayed_event_push(worker_t* worker, event_t* ev, msec_t expire)
{
    ev->expire = expire;
    timer_queue_push(&worker->timer_queue, ev);
}

void worker_delayed_event_update(worker_t* worker, event_t* ev, msec_t expire)
{
    timer_queue_update(&worker->timer_queue, ev, expire);
}

void worker_delayed_event_remove(worker_t* worker, event_t* ev)
{
    timer_queue_remove(&worker->timer_queue, ev);
}

void worker_stop_connection(connection_t* c)
{
    charon_info("connection closed fd=%d, addr=%s:%s", c->fd, c->hbuf, c->sbuf);
    list_remove(&c->node);
    c->worker->connections[c->fd] = NULL;
    close(c->fd);
    c->handler->on_connection_end(c);
    conn_destroy(c);
    free(c);
}

int worker_accept_connections(worker_t* worker, handler_t* handler)
{
    connection_t* c = NULL;
    int res, cnt = 0;
    while ((res = worker_accept_client(worker, handler, &c)) == CHARON_OK) {
        c->handler->on_connection_init(c);
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
    http_handler_on_finish(worker->http_handler);
    close(worker->socket);
    charon_info("worker stopped");
}

void worker_init_handlers(worker_t* worker)
{
    worker->http_handler = http_handler_on_init();
    handler_t* handler = (handler_t*) worker->http_handler;
    config_open(worker->conf_filename, handler->conf, handler->conf_def);
    handler->on_config_done(handler);
}

void worker_loop(worker_t* worker)
{
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event* epoll_event;
    int events_count;
    connection_t* c;

    worker_init_handlers(worker);

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
            if (epoll_event->data.fd == worker->socket) {
                worker_accept_connections(worker, (handler_t*) worker->http_handler);
            } else {
                c = worker->connections[epoll_event->data.fd];
                if (c != NULL && epoll_event->events & EPOLLIN) {
                    c->read_ev.handler(&c->read_ev);
                }

                c = worker->connections[epoll_event->data.fd];
                if (c != NULL && epoll_event->events & EPOLLOUT) {
                    c->write_ev.handler(&c->write_ev);
                }
            }
        }

        event_t* ev;
        charon_debug("processing timeouts");
        while (!timer_queue_empty(&worker->timer_queue) &&
            (ev = timer_queue_top(&worker->timer_queue))->expire <= get_current_msec()) {
            ev->handler(ev);
        }
        charon_debug("event loop end");
    }

    worker_finish(worker);
}

void worker_stop(worker_t* worker)
{
    worker->is_running = false;
}
