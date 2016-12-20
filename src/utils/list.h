#ifndef _LIST_H_
#define _LIST_H_

#include <stdlib.h>

#include "utils/logging.h"

/*
 * Implementation if doubly linked list. The main idea to embed
 * list metadata into user's structure. It helps processor to use
 * caching more effectively.
 */


struct list_node {
    struct list_node* next;
    struct list_node* prev;
};

struct list {
    struct list_node* head;
    struct list_node* tail;
};

/* TODO: make it possible to  place list_node in any field of struct (use offsetof) */

#define list_data(node_ptr, type, node_field) ((type*) (((uint8_t*)(node_ptr)) - offsetof(type, node_field)))
#define list_next(node_ptr) ((node_ptr)->next)
#define list_head(l) ((l)->head)
#define list_tail(l) ((l)->tail)
#define list_foreach(l, var) for (var = list_head(l); var; var = list_next(var))
#define list_foreach_safe(l, var, tmp) for (var = (l)->head, tmp = (var) ? var->next : NULL; var != NULL; var = tmp, tmp = (var) ? var->next : NULL)
#define list_peek(l, type, node_field) list_data(__list_peek(l), type, node_field)

#define LIST_EMPTY (struct list) { NULL, NULL }
#define LIST_NODE_EMPTY (struct list_node) { NULL, NULL }

static inline int list_init(struct list* l)
{
    l->head = NULL;
    l->tail = NULL;
    return 0;
}

static inline struct list* list_create()
{
    struct list* l = (struct list*) malloc(sizeof(struct list));
    list_init(l);
    return l;
}

static inline void list_append(struct list* l, struct list_node* node)
{
    node->next = NULL;
    if (!l->head) {
        l->head = l->tail = node;
    } else {
        l->tail->next = node;
        node->prev = l->tail;
        l->tail = l->tail->next;
    }
}

static inline void list_remove(struct list* l, struct list_node* node)
{
    if (node == l->head) {
        l->head = l->head->next;
        if (l->head == NULL) {
            l->tail = NULL;
        }
    } else if (node == l->tail) {
        l->tail = l->tail->prev;
        if (list->tail == NULL) {
            list->head = NULL;
        } else {
            l->tail->next = NULL;
        }
    } else {
        struct list_node* a = node->prev;
        struct list_node* c = node->next;

        a->next = c;
        c->prev = a;
    }
}

static inline struct list_node* __list_peek(struct list* l)
{
    struct list_node* node = l->head;
    list_remove(l, node);
    node->next = NULL;
    return node;
}

static inline void list_free(struct list* l)
{
    free(l);
}

#endif
