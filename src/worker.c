#define _GNU_SOURCE
#include <arpa/inet.h>
#include <error.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "worker.h"
#include "connection.h"
#include "http/handler.h"
#include "utils/logging.h"
#include "utils/string.h"

int worker_init_handlers(worker_t* worker)
{
    worker->http_handler = http_handler_create();
    return CHARON_OK;
}

worker_t* worker_create()
{
    worker_t* worker = (worker_t*) malloc(sizeof(worker_t));
    worker->socket = -1;
    worker->is_running = 0;
    timer_queue_init(&worker->timer_queue);
    vector_init(&worker->connections);
    vector_init(&worker->children);
    list_head_init(worker->deferred_events);
    worker_init_handlers(worker);
    return worker;
}

void worker_destroy(worker_t* worker)
{
    http_handler_destroy(worker->http_handler);
    timer_queue_destroy(&worker->timer_queue);
    vector_destroy(&worker->connections);
    vector_destroy(&worker->children);
    free(worker->http_handler);
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

int worker_conf_init(worker_conf_t* conf)
{
    conf->port = 0;
    conf->workers = 0;
    return CHARON_OK;
}

static conf_field_def_t main_fields_def[] = {
    CONF_FIELD("port", CONF_INTEGER, CONF_REQUIRED, offsetof(worker_conf_t, port)),
    CONF_FIELD("workers", CONF_INTEGER, CONF_REQUIRED, offsetof(worker_conf_t, workers)),
    CONF_END_FIELDS()
};

static conf_section_def_t worker_conf_def[] = {
    CONF_SECTION("main", CONF_REQUIRED, main_fields_def, worker_conf_init, sizeof(worker_conf_t), 0),
    CONF_END_SECTIONS()
};

int worker_configure(worker_t* worker, char* conf_filename)
{
    handler_t* handler = (handler_t*) worker->http_handler;
    conf_def_t conf_def[] = {
        { &worker->conf, worker_conf_def },
        { worker->http_handler->handler.conf, worker->http_handler->handler.conf_def },
        { NULL, NULL }
    };
    if (config_open(conf_filename, conf_def) != CHARON_OK) {
        return -CHARON_ERR;
    }
    return handler->on_config_done(handler);
}

int worker_start(worker_t* worker)
{
    struct addrinfo hints;
    struct addrinfo* result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char _port[5];
    snprintf(_port, 5, "%d", worker->conf.port);

    int res = getaddrinfo(NULL, _port, &hints, &result);

    if (res != 0) {
        charon_error("getaddrinfo failed: %s", gai_strerror(res));
        return 1;
    }

    for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
        worker->socket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (worker->socket < 0) {
            close(worker->socket);
        } else {
            int enable = 1;
            if (setsockopt(worker->socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
                charon_perror("setsockopt: ");
            }

            if (setsockopt(worker->socket, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
                charon_perror("setsockopt: ");
            }

            res = bind(worker->socket, rp->ai_addr, rp->ai_addrlen);
            if (res == 0) {
                charon_info("charon running on %s:%d", inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr), worker->conf.port);
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

    if (set_fd_non_blocking(worker->socket) == -1) {
        charon_perror("cannot set socket to non-blocking mode: ");
        return 3;
    }

    if (listen(worker->socket, SOMAXCONN) == -1) {
        charon_perror("listen: ");
    }

    return CHARON_OK;
}

int worker_create_event_loop(worker_t* worker) {
    int res;

    worker->epoll_fd = epoll_create1(0);
    if (worker->epoll_fd < 0) {
        charon_perror("epoll_create: ");
        return -CHARON_ERR;
    }

    struct epoll_event ctl_event;
    ctl_event.data.fd = worker->socket;
    ctl_event.events = EPOLLIN | EPOLLET;

    res = epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, worker->socket, &ctl_event);
    if (res < 0) {
        charon_perror("epoll_ctl: ");
        return -CHARON_ERR;
    }

    worker->is_running = true;

    return CHARON_OK;
}

int worker_accept_client(worker_t* worker, handler_t* handler, connection_t** result)
{
    connection_t* c;
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

    // int enable = 1;
    // setsockopt(new_fd, IPPROTO_TCP, TCP_CORK, &enable, sizeof(enable));

    c = handler->create_connection(worker, handler, new_fd);

    memcpy(&c->addr, &addr, in_addr_len);
    worker_add_connection(worker, c);

    if (!getnameinfo((struct sockaddr*) &c->addr, in_addr_len,
                    c->hbuf, sizeof(c->hbuf),
                    c->sbuf, sizeof(c->sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV)) {
        charon_info("accepted connection from %s:%s (fd=%d)", c->hbuf, c->sbuf, new_fd);
    }

    *result = c;

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

void worker_epoll_unregister(worker_t* worker, int fd)
{
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
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

void worker_defer_event(worker_t* worker, event_t* ev)
{
    if (!ev->deferred) {
        charon_debug("deferred added");
        list_insert_last(&worker->deferred_events, &ev->lnode);
        ev->deferred = 1;
    }
}

void worker_deferred_event_remove(UNUSED worker_t* worker, event_t* ev)
{
    if (ev->deferred) {
        charon_debug("deferred remove");
        list_remove(&ev->lnode);
        ev->deferred = 0;
    }
}

void worker_stop_connection(connection_t* c)
{
    charon_info("connection closed fd=%d", c->fd);
    c->worker->connections[c->fd] = NULL;
    close(c->fd);
    if (c->handler) {
        c->handler->destroy_connection(c);
    }
    free(c);
}

void worker_add_connection(worker_t* worker, connection_t* c)
{
    vector_set(&worker->connections, c->fd, &c, connection_t*);
}

void worker_remove_connection(connection_t* c)
{
    c->worker->connections[c->fd] = NULL;
    worker_epoll_unregister(c->worker, c->fd);
}

int worker_accept_connections(worker_t* worker, handler_t* handler)
{
    connection_t* c = NULL;
    int res, cnt = 0;
    for (;;) {
        res = worker_accept_client(worker, handler, &c);
        if (res != CHARON_OK) {
            break;
        }
        cnt++;
    }
    return cnt;
}

void worker_finish(worker_t* worker)
{
    connection_t* c;

    for (size_t i = 0; i < vector_size(&worker->connections); i++) {
        c = worker->connections[i];
        if (c != NULL) {
            close(c->fd);
            if (c->handler != NULL) {
                c->handler->destroy_connection(c);
                charon_debug("handler called");
            }
            free(c);
        }
    }

    charon_info("active connections stopped");
    close(worker->socket);
    charon_info("worker stopped");
}

void worker_loop(worker_t* worker)
{
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event* epoll_event;
    int events_count;
    connection_t* c;
    list_node_t* ptr;
    list_node_t* tmp;

    worker_create_event_loop(worker);

    charon_info("started worker loop");
    while (worker->is_running) {
        charon_debug("processing deferred events");
        list_foreach_safe(&worker->deferred_events, ptr, tmp) {
            event_t* ev = list_entry(ptr, event_t, lnode);
            worker_deferred_event_remove(worker, ev);
            ev->handler(ev);
        }

        msec_t timeout = -1;
        if (!timer_queue_empty(&worker->timer_queue)) {
            timeout = timer_queue_top(&worker->timer_queue)->expire - get_current_msec();
        }

        events_count = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, (int)timeout);

        if (events_count < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                charon_perror("epoll_wait: ");
                worker->is_running = false;
                break;
            }
        }

        charon_debug("%d events in queue", events_count);
        for (int i = 0; i < events_count; i++) {
            epoll_event = events + i;
            if (epoll_event->data.fd == worker->socket) {
                worker_accept_connections(worker, (handler_t*) worker->http_handler);
            } else {
                c = worker->connections[epoll_event->data.fd];
                if (c != NULL && epoll_event->events & EPOLLIN) {
                    charon_debug("read event, fd=%d", epoll_event->data.fd);
                    c->read_ev.handler(&c->read_ev);
                }

                c = worker->connections[epoll_event->data.fd];
                if (c != NULL && epoll_event->events & EPOLLOUT) {
                    charon_debug("write event, fd=%d", epoll_event->data.fd);
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

void worker_stop_signal_handler(UNUSED int sig)
{
    worker_stop(global_server);
}

int worker_setup_signals()
{
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGTERM);
    struct sigaction sigact = {
        .sa_flags = 0,
        .sa_handler = worker_stop_signal_handler,
    };
    if (sigaction(SIGTERM, &sigact, NULL) < 0) {
        charon_perror("sigaction: ");
        return -CHARON_ERR;
    }
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        charon_perror("sigprocmask: ");
        return -CHARON_ERR;
    }
    return CHARON_OK;
}

int worker_run(worker_t* worker)
{
    worker_setup_signals();
    worker->pid = getpid();
    worker_loop(worker);
    worker_destroy(worker);

    return 0;
}

pid_t worker_spawn(worker_t* worker)
{
    pid_t pid = fork();
    if (pid == 0) {
        exit(worker_run(worker));
    } else if (pid < 0) {
        charon_perror("fork: ");
        return -CHARON_ERR;
    } else {
        return pid;
    }
}

int worker_spawn_all(worker_t* worker)
{
    for (int i = 0; i < worker->conf.workers; i++) {
        pid_t pid = worker_spawn(worker);
        if (pid < 0) {
            return -CHARON_ERR;
        }
        charon_info("spawned child process, pid=%d", pid);
        vector_push(&worker->children, &pid, pid_t);
    }
    return CHARON_OK;
}

void worker_kill_all(worker_t* worker, int sig)
{
    for (size_t i = 0; i < vector_size(&worker->children); i++) {
        kill(worker->children[i], sig);
    }
}

int worker_wait_all(UNUSED worker_t* worker)
{
    int status;

    for (;;) {
        pid_t pid = wait(&status);
        if (pid > 0) {
            if (WIFEXITED(status)) {
                charon_info("child process pid=%d exited, exit status=%d",
                    pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                charon_info("child process pid=%d killed by signal %d",
                    pid, WTERMSIG(status));
            }
        } else {
            if (errno == ECHILD) {
                return CHARON_OK;
            } else {
                return CHARON_AGAIN;
            }
        }
    }
}
