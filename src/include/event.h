#ifndef _EVENT_H_
#define _EVENT_H_

#include <stdlib.h>
#include "utils/list.h"
#include "defs.h"

struct connection_s;

typedef enum {
    EV_TIMEOUT,
} event_type_t;

struct event_s {
    size_t timer_queue_idx;

    struct connection_s* conn;
    event_type_t type;
    msec_t expire;
};

typedef struct event_s event_t;

inline static event_t* event_create(struct connection_s* c, event_type_t type)
{
    event_t* ev = malloc(sizeof(event_t));
    if (ev == NULL) {
        return NULL;
    }
    ev->conn = c;
    ev->type = type;
    return ev;
}

inline static void event_destroy(UNUSED event_t* ev)
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
