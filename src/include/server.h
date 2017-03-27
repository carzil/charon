#ifndef _CHARON_SERVER_H_
#define _CHARON_SERVER_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/epoll.h>

#include "http_parser.h"
#include "utils/list.h"
#include "utils/vector.h"
#include "client.h"
#include "conf.h"
#include "timer.h"


enum {
    MAX_EVENTS = 64,
    INITIAL_CLIENT_BUFFER_SIZE = 128,
};

struct worker_s {
    int socket;
    struct sockaddr_in addr;
    int epoll_fd;
    bool is_running;

    config_t* conf;
    timer_queue_t timer_queue;
    LIST_HEAD_DECLARE(clients);
    VECTOR_DEFINE(connections, connection_t*);
};

typedef struct worker_s worker_t;

worker_t* worker_create(config_t* conf);
void worker_destroy(worker_t* worker);
int worker_start(worker_t* worker, int port);
void worker_stop(worker_t* worker);
void worker_loop(worker_t* worker);
void _worker_stop_connection(worker_t* worker, connection_t* c);
#define worker_stop_connection(w, c) do { charon_debug("worker_stop_connection called from %s:%d", __FILE__, __LINE__); _worker_stop_connection(w, c); } while(0)

void worker_enable_read(worker_t* worker, connection_t* c);
void worker_enable_write(worker_t* worker, connection_t* c);
void worker_disable_read(worker_t* worker, connection_t* c);
void worker_disable_write(worker_t* worker, connection_t* c);

void worker_delayed_event_push(worker_t* worker, event_t* ev, msec_t expire);
void worker_delayed_event_update(worker_t* worker, event_t* ev, msec_t expire);
void worker_delayed_event_remove(worker_t* worker, event_t* ev);


extern worker_t* global_server;
int server_main(int argc, char *argv[]);

#endif
