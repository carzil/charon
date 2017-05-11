#ifndef _EVENT_H_
#define _EVENT_H_

#include <stdlib.h>
#include <sys/epoll.h>

#include "utils/list.h"
#include "defs.h"

enum {
    EVENT_READ = 1,
    EVENT_WRITE = 2,
    EVENT_DELAYED = 4
};

struct worker_s;

struct event_s {
    void* data;

    size_t timer_queue_idx;
    msec_t expire;

    int (*handler)(struct event_s* ev);

    int fd;

    unsigned active:1;
    unsigned delayed:1;
    unsigned in_timer:1;
};

typedef struct event_s event_t;

static inline void event_init(event_t* ev)
{
    ev->data = NULL;
    ev->active = 0;
    ev->delayed = 0;
    ev->in_timer = 0;
    ev->expire = -1;
    ev->fd = -1;
    ev->handler = NULL;
}

static inline event_t* event_create()
{
    event_t* ev = malloc(sizeof(event_t));
    if (ev == NULL) {
        return NULL;
    }
    return ev;
}

static inline void event_destroy(UNUSED event_t* ev)
{

}

#endif
