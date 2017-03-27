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

    int (*handler)(struct worker_s* w, struct event_s* ev);

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

// static inline void rb_print(struct rb_node* node, int idt)
// {
//     if (node == NULL) {
//         return;
//     }

//     for (int i = 0; i < idt; i++) {
//         printf("--");
//     }

//     event_t* ev = rb_entry(node, event_t, rnode);
//     if (node->color == RB_RED) {
//         printf("> \033[31m%lld\033[0m (%p)\n", ev->expire, &ev->rnode);
//     } else {
//         printf("> \033[32m%lld\033[0m (%p)\n", ev->expire, &ev->rnode);
//     }
//     for (int i = 0; i < idt; i++) {
//         printf(" ");
//     }
//     printf("left:\n");
//     rb_print(node->left, idt + 1);
//     for (int i = 0; i < idt; i++) {
//         printf(" ");
//     }
//     printf("right:\n");
//     rb_print(node->right, idt + 1);
// }

#endif
