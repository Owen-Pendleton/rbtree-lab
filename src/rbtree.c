#include "rbtree.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum { RB_BLACK = 0, RB_RED = 1 } rb_color_t;

struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    rb_color_t      color;
    char            *key;   /* owned copy, e.g. via strdup */
    void            *value; /* opaque; ownership governed by value_free */
};

struct rbtree {
    struct rb_node   *root;
    struct rb_node    nil;  /* sentinel, embedded by value, always black */
    size_t            size;
    rb_value_free_fn  value_free;
};

rbtree_t *rb_create(rb_value_free_fn value_free)
{
    struct rbtree *t = malloc(sizeof *t);
    if (!t) {
        return NULL;
    }

    t->nil.color  = RB_BLACK;
    t->nil.key    = NULL;
    t->nil.value  = NULL;
    t->nil.left   = &t->nil;
    t->nil.right  = &t->nil;
    t->nil.parent = &t->nil;

    t->root       = &t->nil;
    t->size       = 0;
    t->value_free = value_free;

    return t;
}

static char *dup_key(const char *key)
{
    size_t len = strlen(key) + 1;
    char *copy = malloc(len);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, key, len);
    return copy;
}

int rb_insert(rbtree_t *t, const char *key, void *value)
{
    struct rb_node *y = &t->nil;
    struct rb_node *x = t->root;

    /* invariant: y trails one step behind x; once x reaches nil, y is the
     * insertion point's parent (or nil if the tree is empty) */
    while (x != &t->nil) {
        y = x;
        int cmp = strcmp(key, x->key);
        if (cmp == 0) {
            if (t->value_free) {
                t->value_free(x->value);
            }
            x->value = value;
            return 0;
        }
        x = (cmp < 0) ? x->left : x->right;
    }

    struct rb_node *node = NULL;
    char *key_copy = NULL;
    int rc = -1;

    node = malloc(sizeof *node);
    if (!node) {
        goto cleanup;
    }
    key_copy = dup_key(key);
    if (!key_copy) {
        goto cleanup;
    }

    node->key    = key_copy;
    node->value  = value;
    node->color  = RB_RED;
    node->left   = &t->nil;
    node->right  = &t->nil;
    node->parent = y;

    if (y == &t->nil) {
        t->root = node;
    } else if (strcmp(key, y->key) < 0) {
        y->left = node;
    } else {
        y->right = node;
    }

    t->size++;
    rc = 0;

cleanup:
    if (rc != 0) {
        free(key_copy);
        free(node);
    }
    return rc;
}

/* Checks BST key ordering only; color/black-height invariants are not yet
 * checked here since insert-fixup (which is required to keep them true)
 * doesn't exist yet. */
static int validate_order(const rbtree_t *t, const struct rb_node *node,
                           const char **last)
{
    if (node == &t->nil) {
        return 1;
    }
    if (!validate_order(t, node->left, last)) {
        return 0;
    }
    if (*last != NULL && strcmp(*last, node->key) >= 0) {
        return 0;
    }
    *last = node->key;
    return validate_order(t, node->right, last);
}

int rb_validate(const rbtree_t *t)
{
    const char *last = NULL;
    return validate_order(t, t->root, &last) ? 0 : -1;
}

static void destroy_subtree(rbtree_t *t, struct rb_node *node)
{
    if (node == &t->nil) {
        return;
    }
    destroy_subtree(t, node->left);
    destroy_subtree(t, node->right);
    if (t->value_free) {
        t->value_free(node->value);
    }
    free(node->key);
    free(node);
}

void rb_destroy(rbtree_t *t)
{
    if (!t) {
        return;
    }
    destroy_subtree(t, t->root);
    free(t);
}

void *rb_find(const rbtree_t *t, const char *key)
{
    const struct rb_node *cur = t->root;

    /* invariant: if key is in the tree, it is in the subtree rooted at cur */
    while (cur != &t->nil) {
        int cmp = strcmp(key, cur->key);
        if (cmp == 0) {
            return cur->value;
        }
        cur = (cmp < 0) ? cur->left : cur->right;
    }

    return NULL;
}
