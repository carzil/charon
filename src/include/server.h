#ifndef _CHARON_SERVER_H_
#define _CHARON_SERVER_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/epoll.h>

#include "http_parser.h"
#include "utils/list.h"
#include "client.h"
#include "conf.h"
#include "timer.h"

#define MAX_EVENTS 64
#define INITIAL_CLIENT_BUFFER_SIZE 128

struct worker_s {
    int socket;
    struct sockaddr_in addr;
    int epoll_fd;
    bool is_running;

    config_t* conf;
    LIST_HEAD_DECLARE(clients);
    timer_queue_t timer_queue;
};

typedef struct worker_s worker_t;

int server_main(int argc, char *argv[]);

extern worker_t* global_server;

worker_t* worker_create(config_t* conf);
void worker_destroy(worker_t* worker);
int worker_start(worker_t* worker, int port);
void worker_stop(worker_t* worker);
void worker_loop(worker_t* worker);
void worker_stop_connection(worker_t* worker, connection_t* c);
void worker_schedule_write(worker_t* worker, connection_t* c);
void worker_unschedule_write(worker_t* worker, connection_t* c);

#endif
