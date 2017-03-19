#ifndef _LIST_H_
#define _LIST_H_

#include <stddef.h>

struct list_node {
    struct list_node* next;
    struct list_node* prev;
};

static inline void __list_insert(struct list_node* prev, struct list_node* what, struct list_node* next)
{
    prev->next = what;
    what->prev = prev;
    what->next = next;
    next->prev = what;
}

static inline void __list_delete(struct list_node* prev, struct list_node* next)
{
    prev->next = next;
    next->prev = prev;
}

static inline void list_insert_last(struct list_node* head, struct list_node* what)
{
    __list_insert(head->prev, what, head);
}

static inline void list_insert_first(struct list_node* head, struct list_node* what)
{
    __list_insert(head, what, head->next);
}

static inline void list_remove(struct list_node* what)
{
    __list_delete(what->prev, what->next);
}

static inline void list_init(struct list_node* node)
{
    node->next = node;
    node->prev = node;
}

static inline void list_rotate_left(struct list_node* node)
{
    struct list_node* first = node->next;
    __list_delete(node->prev, node->next);
    list_insert_first(first, node);
}


#define list_entry(ptr, type, node_field) ((type*) (((char*)ptr) - offsetof(type, node_field)) )
#define list_first_entry(head, type, node_field) (list_entry(head.next, type, node_field))
#define list_next_entry(ptr, node_field) (list_entry((ptr)->node_field.next, typeof(*ptr), node_field))
#define list_foreach(head, ptr) for (ptr = (head)->next; ptr != (head); ptr = ptr->next)
#define list_foreach_safe(head, ptr, tmp) for (ptr = (head)->next, tmp = ptr->next; ptr != (head); ptr = tmp, tmp = ptr->next)
#define list_empty(head) ((head) == (head)->next)

#define LIST_HEAD_DECLARE(name) struct list_node name
#define LIST_HEAD_DEFINE(name) struct list_node name = { &name, &name }
#define LIST_HEAD_INIT(head) (head) = ((struct list_node) { &(head), &(head) })
#endif
