#include "rbtree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct {
    const char *name;
    const char *inserts[8];
    size_t      n_inserts;
    const char *delete_key;
    size_t      expect_size_after;
} delete_case_t;

static const delete_case_t delete_cases[] = {
    { "red leaf",
      {"10","20","30"}, 3, "10", 2 },
    /* double-black case 1 (sibling red, x left child): the rotation this
     * branch does always falls straight through into another case in the
     * same delete_fixup call - here, case 4 (far red nephew) - since the
     * loop never revisits case 1 within one call. */
    { "black leaf, red sibling",
      {"61","06","40","91","79","76"}, 6, "06", 5 },
    { "node with two children",
      {"50","30","70","20","40"}, 5, "30", 4 },
    { "root deletion",
      {"50","30"}, 2, "50", 1 },
    { "black node, single red child",
      {"61","06","40","91","79","76"}, 6, "61", 5 },
    /* double-black case 2 (x left child, sibling black, both nephews
     * black): pure recolor-and-climb, isolated with no other branch
     * firing in this delete_fixup call. */
    { "double-black: sibling black, nephews black (left)",
      {"12","14","06","00","02","04","09","08"}, 8, "00", 7 },
    /* double-black case 3 (x left child, sibling black, near nephew red):
     * always falls through into case 4 immediately - case 3 can never
     * fire alone. */
    { "double-black: near-red nephew rotates to far-red (left)",
      {"07","03","13","08"}, 4, "03", 3 },
    /* double-black case 4 (x left child, sibling black, far nephew red),
     * isolated: the far-red nephew is already in place, no case-3
     * rotation needed first. */
    { "double-black: far-red nephew (left)",
      {"05","12","07","13"}, 4, "05", 3 },
    /* mirror image of case 1: sibling red, x right child. Chains into
     * case 2 here (both nephews black after the rotation), rather than
     * case 4 - which follow-up case fires depends on the resulting
     * sibling's shape, not on which side x is on. */
    { "double-black: red sibling (right, mirror)",
      {"15","12","08","05","02","01"}, 6, "12", 5 },
    /* mirror of case 2: sibling black, both nephews black, isolated. */
    { "double-black: sibling black, nephews black (right, mirror)",
      {"01","02","00","14","10","03","07","05"}, 8, "01", 7 },
    /* mirror of case 3: near-red nephew, always falls through to the
     * mirrored case 4. */
    { "double-black: near-red nephew rotates to far-red (right, mirror)",
      {"01","14","12","11"}, 4, "12", 3 },
    /* mirror of case 4: far-red nephew, isolated. */
    { "double-black: far-red nephew (right, mirror)",
      {"07","06","04","01"}, 4, "07", 3 },
};

static void test_delete_cases(void)
{
    for (size_t i = 0; i < sizeof delete_cases / sizeof delete_cases[0]; i++) {
        const delete_case_t *c = &delete_cases[i];
        rbtree_t *t = rb_create(free);
        assert(t != NULL);
        for (size_t j = 0; j < c->n_inserts; j++) {
            assert(rb_insert(t, c->inserts[j], make_int((int)j)) == 0);
        }
        assert(rb_delete(t, c->delete_key) == 0);
        assert(rb_validate(t) == 0);
        assert(rb_size(t) == c->expect_size_after);
        assert(rb_find(t, c->delete_key) == NULL);
        rb_destroy(t);
    }
}

static void count_calls(const char *key, void *value, void *ctx)
{
    (void)key;
    (void)value;
    (*(int *)ctx)++;
}

static void test_empty_tree(void)
{
    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    assert(rb_size(t) == 0);
    assert(rb_find(t, "missing") == NULL);
    assert(rb_delete(t, "missing") == -1);
    assert(rb_validate(t) == 0);

    int calls = 0;
    rb_foreach(t, count_calls, &calls);
    assert(calls == 0);

    rb_destroy(t);
}

static void test_single_node(void)
{
    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    assert(rb_insert(t, "solo", make_int(42)) == 0);
    assert(rb_size(t) == 1);
    assert(*(int *)rb_find(t, "solo") == 42);
    assert(rb_validate(t) == 0);

    assert(rb_delete(t, "solo") == 0);
    assert(rb_size(t) == 0);
    assert(rb_find(t, "solo") == NULL);
    assert(rb_validate(t) == 0);

    /* already gone: the now-empty tree must still handle delete cleanly */
    assert(rb_delete(t, "solo") == -1);

    rb_destroy(t);
}

static void test_overwrite_only_key(void)
{
    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    assert(rb_insert(t, "solo", make_int(1)) == 0);
    assert(rb_size(t) == 1);

    int base = free_count;
    assert(rb_insert(t, "solo", make_int(2)) == 0);
    assert(free_count == base + 1); /* old value freed exactly once */
    assert(rb_size(t) == 1);        /* no phantom second node */
    assert(*(int *)rb_find(t, "solo") == 2);
    assert(rb_validate(t) == 0);

    /* repeated overwrites on the sole node: any key-copy leak on this path
     * is invisible to assert() (the key is never re-copied, and there's no
     * public API to inspect it) but this loop gives make asan/memcheck a
     * heavily-exercised target */
    for (int i = 0; i < 50; i++) {
        assert(rb_insert(t, "solo", make_int(i)) == 0);
    }
    assert(rb_size(t) == 1);
    assert(*(int *)rb_find(t, "solo") == 49);
    assert(rb_validate(t) == 0);

    assert(rb_delete(t, "solo") == 0);
    assert(rb_size(t) == 0);

    rb_destroy(t);
}

static void test_long_keys(void)
{
    enum { LONG_KEY_LEN = 100000 };

    char *long_key = malloc(LONG_KEY_LEN + 1);
    assert(long_key != NULL);
    memset(long_key, 'a', LONG_KEY_LEN);
    long_key[LONG_KEY_LEN - 1] = 'b';
    long_key[LONG_KEY_LEN] = '\0';

    /* same content as long_key, different address: proves comparisons are
     * content-based (strcmp), not pointer-identity */
    char *long_key_copy = malloc(LONG_KEY_LEN + 1);
    assert(long_key_copy != NULL);
    memcpy(long_key_copy, long_key, LONG_KEY_LEN + 1);

    /* differs from long_key only in the final byte */
    char *near_dup = malloc(LONG_KEY_LEN + 1);
    assert(near_dup != NULL);
    memset(near_dup, 'a', LONG_KEY_LEN);
    near_dup[LONG_KEY_LEN] = '\0';

    rbtree_t *t = rb_create(counting_free);
    assert(t != NULL);

    assert(rb_insert(t, long_key, make_int(7)) == 0);
    assert(rb_insert(t, near_dup, make_int(8)) == 0);
    assert(rb_size(t) == 2);
    assert(rb_validate(t) == 0);

    assert(*(int *)rb_find(t, long_key_copy) == 7);
    assert(*(int *)rb_find(t, near_dup) == 8);

    assert(rb_delete(t, long_key_copy) == 0);
    assert(rb_size(t) == 1);
    assert(rb_find(t, long_key) == NULL);
    assert(rb_validate(t) == 0);

    rb_destroy(t);
    free(long_key);
    free(long_key_copy);
    free(near_dup);
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

    /* sorted insertion: without fixup this degenerates into a linked list.
     * Validate and check size after every single insert (not just at the
     * end) to localize a broken fixup at the exact bad insert, and spot-
     * check early keys stay reachable via rb_find - a stale t->root after
     * a root-level rotation could leave rb_validate passing on the
     * (still internally well-formed) orphaned subtree while silently
     * dropping nodes from the reachable tree. */
    rbtree_t *t3 = rb_create(free);
    assert(t3 != NULL);

    char key[3];
    for (int i = 0; i < 20; i++) {
        assert(snprintf(key, sizeof key, "%02d", i) == 2);
        assert(rb_insert(t3, key, make_int(i)) == 0);
        assert(rb_validate(t3) == 0);
        assert(rb_size(t3) == (size_t)(i + 1));
    }
    assert(*(int *)rb_find(t3, "00") == 0);
    assert(*(int *)rb_find(t3, "01") == 1);
    assert(*(int *)rb_find(t3, "10") == 10);
    assert(*(int *)rb_find(t3, "19") == 19);

    rb_destroy(t3);

    test_delete_cases();
    test_empty_tree();
    test_single_node();
    test_overwrite_only_key();
    test_long_keys();

    printf("all tests passed\n");
    return 0;
}
