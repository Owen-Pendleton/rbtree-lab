#include "rbtree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int free_count = 0;

static void counting_free(void *value)
{
    free_count++;
    free(value);
}

static int *make_int(int n)
{
    int *p = malloc(sizeof *p);
    assert(p != NULL);
    *p = n;
    return p;
}

int main(void)
{
    rb_destroy(NULL); /* must not crash */

    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    assert(rb_insert(t, "banana", make_int(2)) == 0);
    assert(rb_insert(t, "apple", make_int(1)) == 0);
    assert(rb_insert(t, "cherry", make_int(3)) == 0);

    assert(*(int *)rb_find(t, "apple") == 1);
    assert(*(int *)rb_find(t, "banana") == 2);
    assert(*(int *)rb_find(t, "cherry") == 3);
    assert(rb_find(t, "durian") == NULL);

    assert(rb_validate(t) == 0);

    /* overwriting an existing key must release the old value via value_free */
    assert(free_count == 0);
    assert(rb_insert(t, "apple", make_int(99)) == 0);
    assert(free_count == 1);
    assert(*(int *)rb_find(t, "apple") == 99);

    assert(rb_validate(t) == 0);

    rb_destroy(t);
    assert(free_count == 4); /* 1 overwrite + 3 remaining nodes */

    /* value_free == NULL: tree does not own these values */
    rbtree_t *t2 = rb_create(NULL);
    assert(t2 != NULL);

    static int nums[2] = {10, 20};
    assert(rb_insert(t2, "x", &nums[0]) == 0);
    assert(rb_insert(t2, "y", &nums[1]) == 0);
    assert(*(int *)rb_find(t2, "x") == 10);
    assert(*(int *)rb_find(t2, "y") == 20);
    assert(rb_validate(t2) == 0);

    rb_destroy(t2);

    printf("all tests passed\n");
    return 0;
}
