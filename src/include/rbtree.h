#ifndef _RBTREE_H_
#define _RBTREE_H_

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

typedef enum {
    RB_RED,
    RB_BLACK
} rb_color_t;

struct rb_node {
    struct rb_node* left;
    struct rb_node* right;
    struct rb_node* parent;
    rb_color_t color;
};

struct rb_tree {
    struct rb_node* root;
};

typedef struct rb_node rb_node_t;
typedef struct rb_tree rb_tree_t;

#define rb_entry(ptr, type, node_field) ((type*) (((char*)ptr) - offsetof(type, node_field)) )
#define DECLARE_RB_ROOT(name) struct rb_tree name
#define DEFINE_RB_ROOT(name) struct rb_tree name = { NULL }

#define RB_NODE_EMPTY ((rb_node_t) { NULL, NULL, NULL, RB_RED })

void rb_insert(rb_node_t** where, rb_tree_t* root, rb_node_t* node, rb_node_t* parent);
void rb_remove(struct rb_tree* tree, struct rb_node* node);

static inline int rb_is_empty(struct rb_tree* tree)
{
    return tree->root == NULL;
}

static inline void rb_tree_init(struct rb_tree* tree)
{
    tree->root = NULL;
}

static inline void rb_make_node_empty(struct rb_node* node)
{
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;
}

#endif
