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

static void rotate_left(rbtree_t *t, struct rb_node *x)
{
    struct rb_node *y = x->right;

    x->right = y->left;
    if (y->left != &t->nil) {
        y->left->parent = x;
    }

    y->parent = x->parent;
    if (x->parent == &t->nil) {
        t->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

static void rotate_right(rbtree_t *t, struct rb_node *x)
{
    struct rb_node *y = x->left;

    x->left = y->right;
    if (y->right != &t->nil) {
        y->right->parent = x;
    }

    y->parent = x->parent;
    if (x->parent == &t->nil) {
        t->root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }

    y->right = x;
    x->parent = y;
}

static void insert_fixup(rbtree_t *t, struct rb_node *z)
{
    /* invariant: z is red; whenever z's parent is also red, z is the one
     * remaining red-red violation, and z's grandparent is always well
     * defined (a red parent can never be the root, since the root is
     * always black at the top of each iteration) */
    while (z->parent->color == RB_RED) {
        if (z->parent == z->parent->parent->left) {
            struct rb_node *y = z->parent->parent->right; /* uncle */
            if (y->color == RB_RED) {
                z->parent->color = RB_BLACK;
                y->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotate_left(t, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rotate_right(t, z->parent->parent);
            }
        } else {
            struct rb_node *y = z->parent->parent->left; /* uncle */
            if (y->color == RB_RED) {
                z->parent->color = RB_BLACK;
                y->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(t, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rotate_left(t, z->parent->parent);
            }
        }
    }
    t->root->color = RB_BLACK;
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

    insert_fixup(t, node);
    t->size++;
    rc = 0;

cleanup:
    if (rc != 0) {
        free(key_copy);
        free(node);
    }
    return rc;
}

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

/* Returns the subtree's black-height (nil counts as black), or -1 if a
 * red-black invariant is violated anywhere below node: a red node with a
 * red child, or the two children's black-heights disagreeing. -1
 * propagates straight up through every ancestor once found. */
static int black_height(const rbtree_t *t, const struct rb_node *node)
{
    if (node == &t->nil) {
        return 1;
    }
    if (node->color == RB_RED &&
        (node->left->color == RB_RED || node->right->color == RB_RED)) {
        return -1;
    }

    int left_bh = black_height(t, node->left);
    if (left_bh == -1) {
        return -1;
    }
    int right_bh = black_height(t, node->right);
    if (right_bh == -1 || right_bh != left_bh) {
        return -1;
    }

    return left_bh + (node->color == RB_BLACK ? 1 : 0);
}

int rb_validate(const rbtree_t *t)
{
    const char *last = NULL;
    if (!validate_order(t, t->root, &last)) {
        return -1;
    }
    if (t->root != &t->nil && t->root->color != RB_BLACK) {
        return -1;
    }
    if (t->root->parent != &t->nil) {
        return -1;
    }
    return black_height(t, t->root) == -1 ? -1 : 0;
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

size_t rb_size(const rbtree_t *t)
{
    return t->size;
}

static void foreach_subtree(const rbtree_t *t, const struct rb_node *node,
                             void (*fn)(const char *, void *, void *),
                             void *ctx)
{
    if (node == &t->nil) {
        return;
    }
    foreach_subtree(t, node->left, fn, ctx);
    fn(node->key, node->value, ctx);
    foreach_subtree(t, node->right, fn, ctx);
}

void rb_foreach(const rbtree_t *t,
                void (*fn)(const char *key, void *value, void *ctx),
                void *ctx)
{
    foreach_subtree(t, t->root, fn, ctx);
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
