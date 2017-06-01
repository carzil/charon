#ifndef _CHARON_SERVER_H_
#define _CHARON_SERVER_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/types.h>

#include "http/parser.h"
#include "http/handler.h"
#include "utils/list.h"
#include "utils/vector.h"
#include "connection.h"
#include "conf.h"
#include "timer.h"


enum {
    MAX_EVENTS = 64,
};

typedef struct {
    int port;
    int workers;
} worker_conf_t;

struct worker_s {
    int socket;
    struct sockaddr_in addr;
    int epoll_fd;
    bool is_running;
    http_handler_t* http_handler;
    pid_t pid;

    worker_conf_t conf;

    timer_queue_t timer_queue;
    VECTOR_DEFINE(connections, connection_t*);
    LIST_HEAD_DECLARE(deferred_events);

    VECTOR_DEFINE(children, pid_t);
};

typedef struct worker_s worker_t;

// TODO: move to utils
int set_fd_non_blocking(int fd);

worker_t* worker_create();
int worker_configure(worker_t* worker, char* conf_filename);
void worker_destroy(worker_t* worker);
int worker_start(worker_t* worker);
int worker_run(worker_t* worker);
void worker_stop(worker_t* worker);
void worker_loop(worker_t* worker);
void worker_stop_connection(connection_t* c);

void worker_add_connection(worker_t* worker, connection_t* c);
void worker_remove_connection(connection_t* c);

void worker_enable_read(connection_t* c);
void worker_enable_write(connection_t* c);
void worker_disable_read(connection_t* c);
void worker_disable_write(connection_t* c);

void worker_delayed_event_push(worker_t* worker, event_t* ev, msec_t expire);
void worker_delayed_event_update(worker_t* worker, event_t* ev, msec_t expire);
void worker_delayed_event_remove(worker_t* worker, event_t* ev);

void worker_defer_event(worker_t* worker, event_t* ev);
void worker_deferred_event_remove(worker_t* worker, event_t* ev);

int worker_spawn_all(worker_t* worker);
void worker_kill_all(worker_t* worker, int sig);
int worker_wait_all(worker_t* worker);

extern worker_t* global_server;
int server_main(int argc, char *argv[]);

#endif
