#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rbtree.h"
#include "utils/logging.h"
#include "event.h"

/* TODO: make asserts */

#define POISON_NODE(node) node->left = (void*)0xdeadbeefdeadbeef; node->right = (void*)0xdeadbeefdeadbeef; node->parent = (void*)0xdeadbeefdeadbeef

void rb_swap_parent(struct rb_node* a, struct rb_node* b)
{
    struct rb_node* parent = a->parent;
    a->parent = b;
    b->parent = parent;
}

void rb_fix_parent(struct rb_tree* tree, struct rb_node* parent, struct rb_node* old, struct rb_node* new)
{
    if (parent != NULL) {
        if (parent->left == old) {
            parent->left = new;
        } else {
            parent->right = new;
        }
    } else {
        tree->root = new;
    }
}

struct rb_node* rb_left_rotate(struct rb_tree* tree, struct rb_node* a)
{
    /*
     * Left rotate:
     *    a               b
     *   / \             / \
     *  c   b     ->    a   e
     *     / \         / \
     *    d   e       c   d
     */
    struct rb_node* b = a->right;
    struct rb_node* d = b->left;
    a->right = b->left;
    b->left = a;
    rb_swap_parent(a, b);
    if (d) {
        d->parent = a;
    }
    rb_fix_parent(tree, b->parent, a, b);
    return b;
}

struct rb_node* rb_right_rotate(struct rb_tree* tree, struct rb_node* a)
{
    /*
     * Right rotate:
     *      a             b
     *     / \           / \
     *    b   c   ->    d   a
     *   / \               / \
     *  d   e             e   c
     */
    struct rb_node* b = a->left;
    struct rb_node* e = b->right;
    a->left = b->right;
    b->right = a;
    rb_swap_parent(a, b);
    if (e) {
        e->parent = a;
    }
    rb_fix_parent(tree, b->parent, a, b);
    return b;
}

void rb_swap_color(struct rb_node* a, struct rb_node* b)
{
    rb_color_t tmp = a->color;
    a->color = b->color;
    b->color = tmp;
}

void __rb_insert(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* parent;
    struct rb_node* grandpa;
    struct rb_node* uncle;
    struct rb_node* new_head;

    for (;;) {
        parent = node->parent;

        if (parent == NULL) {
            node->color = RB_BLACK;
            break;
        }
        if (parent->color == RB_BLACK) {
            break;
        }

        /* Grandpa is always non-NULL, cause parent is red (only root has no parent) */
        grandpa = parent->parent;
        if (parent == grandpa->left) {
            uncle = grandpa->right;
            /*
             * We have the following case:
             *      g
             *     / \
             *    p   u
             *   /
             *  n
             */
            if (uncle != NULL && uncle->color == RB_RED) {
                /* both parent and uncle are red, we can just recolor them and continue */
                uncle->color = RB_BLACK;
                parent->color = RB_BLACK;
                grandpa->color = RB_RED;
                parent = grandpa->parent;
                node = grandpa;
            } else {
                /* uncle is black, rotates are needed */
                new_head = parent;
                if (parent->left != node) {
                    rb_left_rotate(tree, parent);
                }
                new_head = rb_right_rotate(tree, grandpa);
                rb_swap_color(new_head, new_head->right);
                rb_fix_parent(tree, new_head->parent, grandpa, new_head);
                break;
            }
        } else {
            /* symmetric case */
            uncle = grandpa->left;
            if (uncle != NULL && uncle->color == RB_RED) {
                uncle->color = RB_BLACK;
                parent->color = RB_BLACK;
                grandpa->color = RB_RED;
                parent = grandpa->parent;
                node = grandpa;
            } else {
                new_head = parent;
                if (parent->right != node) {
                    rb_right_rotate(tree, parent);
                }
                new_head = rb_left_rotate(tree, grandpa);
                rb_swap_color(new_head, new_head->left);
                rb_fix_parent(tree, new_head->parent, grandpa, new_head);
                break;
            }
        }
    }
}

void rb_insert(rb_node_t** where, struct rb_tree* tree, struct rb_node* node, struct rb_node* parent) {
    node->color = RB_RED;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    *where = node;
    __rb_insert(tree, node);
}

struct rb_node* rb_sibling(struct rb_node* node)
{
    if (node->parent->left == node) {
        return node->parent->right;
    } else {
        return node->parent->left;
    }
}

int rb_has_red_child(struct rb_node* node)
{
    return (node->left != NULL && node->left->color == RB_RED) ||
           (node->right != NULL && node->right->color == RB_RED);
}

/*
 * Internal remove routine, which fixes black violations,
 * node_ptr points to double blacked node.
 */
void __rb_remove_rebalance(struct rb_tree* tree, struct rb_node* node, struct rb_node* parent)
{
    struct rb_node* sibling;
    for (;;) {
        /* invariant: node is double black */
        if (parent == NULL) {
            node->color = RB_BLACK;
            break;
        }

        sibling = parent->left;
        if (node == sibling) {
            sibling = parent->right;
            if (sibling->color == RB_RED) {
                rb_left_rotate(tree, parent);
            } else {
                if (sibling->left != NULL && sibling->left->color == RB_RED) {
                    /* Right-Left case */
                    sibling->color = RB_RED;
                    sibling->left->color = RB_BLACK;
                    sibling = sibling->left;
                    rb_right_rotate(tree, sibling);
                }

                if (sibling->right != NULL && sibling->right->color == RB_RED) {
                    /* Right-Right case */
                    rb_left_rotate(tree, parent);
                    if (sibling->right != NULL) {
                        sibling->right->color = RB_BLACK;
                    }
                    break;
                } else {
                    /* both sibling childs are black */
                    node = parent;
                    parent = parent->parent;
                }
            }
        } else {
            if (sibling->color == RB_RED) {
                rb_right_rotate(tree, parent);
            } else {
                if (sibling->right != NULL && sibling->right->color == RB_RED) {
                    /* Left-Right case */
                    sibling->color = RB_RED;
                    sibling->right->color = RB_BLACK;
                    sibling = sibling->right;
                    rb_left_rotate(tree, sibling);
                }

                if (sibling->left != NULL && sibling->left->color == RB_RED) {
                    rb_right_rotate(tree, parent);
                    if (sibling->left != NULL) {
                        sibling->left->color = RB_BLACK;
                    }
                    break;
                } else {
                    node = parent;
                    parent = parent->parent;
                }
            }
        }
    }
}

void rb_remove(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* parent = node->parent;
    struct rb_node* child = (node->left != NULL) ? node->left : node->right;
    int double_blacked = 0;

    if (node->left == (void*)0xdeadbeefdeadbeef) {
        charon_error("trying to remove poisoned node!");
        exit(100);
    }

    if (node->left != NULL && node->right != NULL) {
        /* node has both child, in this case we need to make it list
         * and then apply standard red-black tree removal */
        struct rb_node* ptr = node->right;
        while (ptr->left != NULL) {
            ptr = ptr->left;
        }

        if (ptr != node->right) {
            ptr->parent->left = ptr->right;
            ptr->right = node->right;
        } else {
            ptr->right = NULL;
        }

        ptr->left = node->left;
        ptr->parent = node->parent;

        rb_fix_parent(tree, parent, node, ptr);

        double_blacked = node->color == RB_BLACK && ptr->color == RB_BLACK;

        ptr->color = node->color;

        POISON_NODE(node);

        node = ptr;
        parent = ptr->parent;
    } else {
        if (parent == NULL) {
            tree->root = child;
        } else if (parent->left == node) {
            parent->left = child;
        } else {
            parent->right = child;
        }
        double_blacked = node->color == RB_BLACK && (child == NULL || child->color == RB_BLACK);
        if (child != NULL) {
            child->parent = parent;
            child->color = RB_BLACK;
        }
        POISON_NODE(node);
        node = child;
    }

    if (node && double_blacked) {
        __rb_remove_rebalance(tree, node, parent);
    }
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
