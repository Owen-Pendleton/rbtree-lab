#include "rbtree.h"
#include "fault_alloc.h"
#include "../src/pool.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int free_count = 0;

/* Which constructor the tree battery uses; main runs it once per kind. */
static rbtree_t *(*create_tree)(rb_value_free_fn) = rb_create;

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
        rbtree_t *t = create_tree(free);
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
    rbtree_t *t = create_tree(counting_free);
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
    rbtree_t *t = create_tree(counting_free);
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
    rbtree_t *t = create_tree(counting_free);
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

    rbtree_t *t = create_tree(counting_free);
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

enum { N_KEYS = 200, N_DELETE = 100, N_OVERWRITE = 50, MAX_SWEEP = 100000 };
enum { KEY_CAP = 16 };

/* shadow model: what the tree must contain right now */
static bool present[N_KEYS];
static int  expect[N_KEYS];

static void make_key(char buf[KEY_CAP], int i)
{
    snprintf(buf, KEY_CAP, "%03d", i);
}

struct walk {
    int    next;    /* lowest model index not yet accounted for */
    size_t matched; /* entries seen so far */
};

static void check_entry(const char *key, void *value, void *ctx)
{
    struct walk *w = ctx;

    /* invariant: every model index below w->next is absent or matched */
    while (w->next < N_KEYS && !present[w->next]) {
        w->next++;
    }
    assert(w->next < N_KEYS);

    char want[KEY_CAP];
    make_key(want, w->next);
    assert(strcmp(key, want) == 0);
    assert(*(int *)value == expect[w->next]);

    w->next++;
    w->matched++;
}

/* The tree must be a valid red-black tree holding exactly the model. */
static void check_tree(const rbtree_t *t)
{
    assert(rb_validate(t) == 0);

    size_t n = 0;
    for (int i = 0; i < N_KEYS; i++) {
        n += present[i] ? 1 : 0;
    }
    assert(rb_size(t) == n);

    struct walk w = { 0, 0 };
    rb_foreach(t, check_entry, &w);
    assert(w.matched == n);
}

/* fault_alloc_total() value at which the armed fault fires */
static long fire_at;

/* Did the armed fault fire since `before` was read? */
static bool fault_hit(long before)
{
    return before < fire_at && fault_alloc_total() >= fire_at;
}

/* Insert (or overwrite) key i with value v. Returns whether this call hit
 * the fault. */
static bool try_insert(rbtree_t *t, int i, int v)
{
    char key[KEY_CAP];
    make_key(key, i);

    int *p = malloc(sizeof *p);
    assert(p != NULL);
    *p = v;

    bool overwrite = present[i];

    long before = fault_alloc_total();
    int frees_before = free_count;
    int rc = rb_insert(t, key, p);
    bool hit = fault_hit(before);

    /* an overwrite reuses the node and key copy the tree already owns */
    if (overwrite) {
        assert(fault_alloc_total() == before);
    }

    if (hit) {
        assert(rc == -1);
        assert(free_count == frees_before); /* value not consumed */
        free(p); /* not consumed: the caller still owns the value */
    } else {
        assert(rc == 0);
        /* an overwrite frees the old value once; a new key frees nothing */
        assert(free_count == frees_before + (present[i] ? 1 : 0));
        present[i] = true;
        expect[i] = v;
    }
    check_tree(t);
    return hit;
}

/* Delete key i, which may be absent if its insert was the one that failed. */
static void try_delete(rbtree_t *t, int i)
{
    char key[KEY_CAP];
    make_key(key, i);

    long before = fault_alloc_total();
    int frees_before = free_count;
    int rc = rb_delete(t, key);

    /* delete has no error code for a failed allocation, so it may not
     * allocate at all */
    assert(fault_alloc_total() == before);

    assert(rc == (present[i] ? 0 : -1));
    assert(free_count == frees_before + (present[i] ? 1 : 0));
    present[i] = false;
    check_tree(t);
}

/* The i-th key of the scenario. 37 shares no factor with N_KEYS, so
 * i = 0..N_KEYS-1 visits every key exactly once, in scrambled order. */
static int scenario_key(int i)
{
    return (i * 37) % N_KEYS;
}

/* One run: insert N_KEYS keys, delete N_DELETE, overwrite N_OVERWRITE, then
 * re-insert the N_DELETE deleted keys, with the n-th allocation failing.
 * Returns whether the fault was hit. */
static bool run_scenario(long n)
{
    bool hit = false;
    long live_before = fault_alloc_live();
    memset(present, 0, sizeof present);

    fire_at = fault_alloc_total() + n;
    fault_alloc_arm(n);

    long before = fault_alloc_total();
    rbtree_t *t = create_tree(counting_free);
    if (fault_hit(before)) {
        assert(t == NULL);
        fault_alloc_disarm();
        assert(fault_alloc_live() == live_before);
        return true;
    }
    assert(t != NULL);
    check_tree(t);

    for (int i = 0; i < N_KEYS; i++) {
        int k = scenario_key(i);
        hit |= try_insert(t, k, k);
    }
    for (int i = 0; i < N_DELETE; i++) {
        try_delete(t, scenario_key(i));
    }
    for (int i = N_DELETE; i < N_DELETE + N_OVERWRITE; i++) {
        int k = scenario_key(i);
        hit |= try_insert(t, k, k + 1000);
    }
    /* invariant: keys scenario_key(0..i-1) are back in the tree unless
     * their re-insert was the one that faulted */
    for (int i = 0; i < N_DELETE; i++) {
        int k = scenario_key(i);
        hit |= try_insert(t, k, k + 2000);
    }

    fault_alloc_disarm();
    int frees_before = free_count;
    int remaining = (int)rb_size(t);
    rb_destroy(t);
    assert(free_count == frees_before + remaining); /* one free per entry */
    assert(fault_alloc_live() == live_before); /* nothing leaked this run */
    return hit;
}

/* expect_faulted: one fault point per rb_malloc a clean run makes, so a
 * change in how often the tree allocates fails here */
static void test_fault_sweep(long expect_faulted)
{
    long n = 1;

    /* invariant: every run with a fault index below n hit its fault */
    while (run_scenario(n)) {
        printf("sweep n=%ld: faulted, ok\n", n);
        fflush(stdout); /* keep the line if a later assert aborts */
        n++;
        assert(n < MAX_SWEEP);
    }

    printf("fault sweep passed: %ld faulted runs, clean at n=%ld\n", n - 1, n);
    fflush(stdout); /* show the count even if the next assert aborts */
    assert(n - 1 == expect_faulted);
}

typedef struct {
    size_t obj_size;
    int    expect_rc;
    size_t stride;
    size_t slots;
    size_t first_off;
} geom_case_t;

/* Hand-computed for a 4096-byte slab, 16-byte max alignment and 8-byte
 * pointers: 16 bytes of header, 4080 bytes of room. */
static const geom_case_t geom_cases[] = {
    { 0,        -1, 0,    0,   0  },
    { 1,         0, 16,   255, 16 }, /* grown to hold the link */
    { 8,         0, 16,   255, 16 },
    { 16,        0, 16,   255, 16 },
    { 17,        0, 32,   127, 16 }, /* 4080 / 32 = 127.5 */
    { 48,        0, 48,   85,  16 }, /* struct rb_node; 85 * 48 = 4080 */
    { 49,        0, 64,   63,  16 },
    { 4080,      0, 4080, 1,   16 }, /* largest that fits */
    { 4081,     -1, 0,    0,   0  },
    { SIZE_MAX, -1, 0,    0,   0  }, /* must not overflow in rounding */
};

static void test_pool_geometry(void)
{
    const size_t align = alignof(max_align_t);
    const struct pool_geom untouched = { 7, 7, 7, 7 };

    /* the table's numbers assume this platform */
    assert(align == 16 && sizeof(void *) == 8);
    assert(POOL_SLAB_SIZE == 4096);

    for (size_t i = 0; i < sizeof geom_cases / sizeof geom_cases[0]; i++) {
        const geom_case_t *c = &geom_cases[i];
        struct pool_geom g = untouched;

        assert(pool_geometry(c->obj_size, &g) == c->expect_rc);
        if (c->expect_rc != 0) {
            assert(memcmp(&g, &untouched, sizeof g) == 0);
            continue;
        }
        assert(g.stride == c->stride);
        assert(g.slots == c->slots);
        assert(g.first_off == c->first_off);
        assert(g.link_off == 0);
    }

    /* invariant: every obj_size below n has a layout that is aligned, holds
     * the object and the link, stays inside the slab, and wastes no slot */
    for (size_t n = 1; n <= 4080; n++) {
        struct pool_geom g;

        assert(pool_geometry(n, &g) == 0);
        assert(g.stride >= n);
        assert(g.stride % align == 0);
        assert(g.first_off % align == 0);
        assert(g.first_off >= sizeof(void *)); /* room for the slab header */
        assert(g.link_off + sizeof(void *) <= g.stride);
        assert(g.slots >= 1);
        assert(g.first_off + g.slots * g.stride <= POOL_SLAB_SIZE);
        assert(g.first_off + (g.slots + 1) * g.stride > POOL_SLAB_SIZE);
    }
}

/* live + free_objs == slabs * slots, with slots taken from pool_geometry so
 * the test and the pool cannot disagree on the arithmetic */
static void check_pool(const rb_pool_t *p, size_t obj_size)
{
    struct pool_geom g;
    size_t slabs, live, free_objs;

    assert(pool_geometry(obj_size, &g) == 0);
    pool_stats(p, &slabs, &live, &free_objs);
    assert(live + free_objs == slabs * g.slots);
}

static void test_pool_empty(void)
{
    const size_t obj_size = 48;
    long total_before = fault_alloc_total();
    long live_before  = fault_alloc_live();

    pool_destroy(NULL); /* must not crash */

    /* a size with no valid geometry is refused before any allocation */
    assert(pool_create(0) == NULL);
    assert(pool_create(4081) == NULL);
    assert(pool_create(SIZE_MAX) == NULL);
    assert(fault_alloc_total() == total_before);

    /* the one allocation fails: NULL, nothing left behind */
    fault_alloc_arm(1);
    assert(pool_create(obj_size) == NULL);
    fault_alloc_disarm();
    assert(fault_alloc_total() == total_before + 1);
    assert(fault_alloc_live() == live_before);

    rb_pool_t *p = pool_create(obj_size);
    assert(p != NULL);
    assert(fault_alloc_live() == live_before + 1); /* the pool, no slab */

    size_t slabs = 9, live = 9, free_objs = 9;
    pool_stats(p, &slabs, &live, &free_objs);
    assert(slabs == 0 && live == 0 && free_objs == 0);
    check_pool(p, obj_size);

    /* each out-pointer is optional */
    pool_stats(p, NULL, NULL, NULL);
    slabs = 9;
    pool_stats(p, &slabs, NULL, NULL);
    assert(slabs == 0);

    pool_destroy(p);
    assert(fault_alloc_live() == live_before);
}

#define POOL_TEST_CAP 256 /* more than 255, the most slots a slab can have */

static void expect_stats(const rb_pool_t *p,
                         size_t slabs, size_t live, size_t free_objs)
{
    size_t s, l, f;

    pool_stats(p, &s, &l, &f);
    assert(s == slabs);
    assert(l == live);
    assert(f == free_objs);
}

/* object i is filled with this byte, so overlapping slots show up */
static unsigned char pattern(size_t i)
{
    return (unsigned char)(i + 1);
}

static void check_patterns(void *const objs[], size_t n, size_t obj_size)
{
    /* invariant: objects before i still hold their own pattern in full */
    for (size_t i = 0; i < n; i++) {
        const unsigned char *b = objs[i];
        for (size_t j = 0; j < obj_size; j++) {
            assert(b[j] == pattern(i));
        }
    }
}

static void test_pool_alloc_size(size_t obj_size)
{
    const size_t align = alignof(max_align_t);
    struct pool_geom g;
    void *objs[POOL_TEST_CAP + 1];
    long live_before = fault_alloc_live();

    assert(pool_geometry(obj_size, &g) == 0);
    assert(g.slots <= POOL_TEST_CAP);

    rb_pool_t *p = pool_create(obj_size);
    assert(p != NULL);

    /* the very first grow fails: NULL, pool still empty */
    fault_alloc_arm(1);
    assert(pool_alloc(p) == NULL);
    fault_alloc_disarm();
    expect_stats(p, 0, 0, 0);
    check_pool(p, obj_size);

    /* fill the first slab exactly.
     * invariant: i objects are live, all from one slab */
    for (size_t i = 0; i < g.slots; i++) {
        long before = fault_alloc_total();

        objs[i] = pool_alloc(p);
        assert(objs[i] != NULL);
        /* only the first one may call rb_malloc; the rest are pops */
        assert(fault_alloc_total() == before + (i == 0 ? 1 : 0));
        expect_stats(p, 1, i + 1, g.slots - i - 1);
        check_pool(p, obj_size);
    }

    uintptr_t lowest = (uintptr_t)objs[0];
    /* invariant: lowest is the smallest address among objects before i */
    for (size_t i = 1; i < g.slots; i++) {
        if ((uintptr_t)objs[i] < lowest) {
            lowest = (uintptr_t)objs[i];
        }
    }

    bool seen[POOL_TEST_CAP] = { false };
    /* invariant: objects before i are aligned, distinct, and each sits a
     * whole number of strides (fewer than slots) above the lowest, which
     * keeps every one of them out of the tail slack */
    for (size_t i = 0; i < g.slots; i++) {
        uintptr_t a   = (uintptr_t)objs[i];
        size_t    off = (size_t)(a - lowest);

        assert(a % align == 0);
        assert(off % g.stride == 0);
        assert(off / g.stride < g.slots);
        assert(!seen[off / g.stride]);
        seen[off / g.stride] = true;
    }

    /* invariant: objects before i are filled with their own pattern */
    for (size_t i = 0; i < g.slots; i++) {
        memset(objs[i], pattern(i), obj_size);
    }
    check_patterns(objs, g.slots, obj_size);

    /* slab full and the next grow fails: NULL, nothing changes */
    fault_alloc_arm(1);
    assert(pool_alloc(p) == NULL);
    fault_alloc_disarm();
    expect_stats(p, 1, g.slots, 0);
    check_pool(p, obj_size);
    check_patterns(objs, g.slots, obj_size);

    /* one more than a slab holds: second slab */
    objs[g.slots] = pool_alloc(p);
    assert(objs[g.slots] != NULL);
    expect_stats(p, 2, g.slots + 1, g.slots - 1);
    check_pool(p, obj_size);
    assert((uintptr_t)objs[g.slots] % align == 0);
    memset(objs[g.slots], pattern(g.slots), obj_size);
    check_patterns(objs, g.slots + 1, obj_size);

    /* destroy with every object still live: both slabs must go */
    assert(fault_alloc_live() == live_before + 3);
    pool_destroy(p);
    assert(fault_alloc_live() == live_before);
}

static void test_pool_alloc(void)
{
    test_pool_alloc_size(48);   /* struct rb_node: no tail slack */
    test_pool_alloc_size(49);   /* 63 slots, 48 bytes of tail slack */
    test_pool_alloc_size(1);    /* 255 slots, link bigger than the object */
    test_pool_alloc_size(4080); /* one slot per slab: every alloc grows */
}

/* last in, first out, on a pool of its own */
static void test_pool_free_lifo(size_t obj_size)
{
    long live_before = fault_alloc_live();
    size_t slabs;

    rb_pool_t *p = pool_create(obj_size);
    assert(p != NULL);

    void *a = pool_alloc(p);
    void *b = pool_alloc(p);
    void *c = pool_alloc(p);
    assert(a && b && c);
    pool_stats(p, &slabs, NULL, NULL);
    check_pool(p, obj_size);

    long total = fault_alloc_total();

    pool_free(p, b);
    check_pool(p, obj_size);
    assert(pool_alloc(p) == b);
    check_pool(p, obj_size);

    pool_free(p, a);
    pool_free(p, c);
    check_pool(p, obj_size);
    assert(pool_alloc(p) == c);
    assert(pool_alloc(p) == a);
    check_pool(p, obj_size);

    /* NULL is a no-op */
    size_t live, free_objs;
    pool_stats(p, NULL, &live, &free_objs);
    pool_free(p, NULL);
    expect_stats(p, slabs, live, free_objs);
    assert(live == 3);

    /* none of that touched rb_malloc, and no slab came or went */
    assert(fault_alloc_total() == total);
    expect_stats(p, slabs, 3, free_objs);

    pool_destroy(p);
    assert(fault_alloc_live() == live_before);
}

static void test_pool_free_size(size_t obj_size)
{
    struct pool_geom g;
    void *objs[2 * POOL_TEST_CAP];
    bool  dead[2 * POOL_TEST_CAP] = { false };
    long  live_before = fault_alloc_live();

    assert(pool_geometry(obj_size, &g) == 0);
    assert(g.slots <= POOL_TEST_CAP);
    const size_t n = 2 * g.slots; /* exactly two slabs' worth */

    test_pool_free_lifo(obj_size);

    rb_pool_t *p = pool_create(obj_size);
    assert(p != NULL);

    /* invariant: i objects are live */
    for (size_t i = 0; i < n; i++) {
        objs[i] = pool_alloc(p);
        assert(objs[i] != NULL);
        check_pool(p, obj_size);
    }
    expect_stats(p, 2, n, 0);

    long total = fault_alloc_total();

    /* free everything: slabs stay, every slot ends up free.
     * invariant: i objects have been returned */
    for (size_t i = 0; i < n; i++) {
        pool_free(p, objs[i]);
        expect_stats(p, 2, n - i - 1, i + 1);
        check_pool(p, obj_size);
    }

    /* take them all again: same addresses, no new slab, no rb_malloc.
     * invariant: the i objects taken so far are distinct members of objs */
    bool taken[2 * POOL_TEST_CAP] = { false };
    for (size_t i = 0; i < n; i++) {
        void  *o = pool_alloc(p);
        size_t j = 0;

        /* invariant: o is none of objs[0..j-1] */
        while (j < n && objs[j] != o) {
            j++;
        }
        assert(j < n);
        assert(!taken[j]);
        taken[j] = true;
        expect_stats(p, 2, i + 1, n - i - 1);
        check_pool(p, obj_size);
    }
    assert(fault_alloc_total() == total);

    /* invariant: objects before i hold their own pattern */
    for (size_t i = 0; i < n; i++) {
        memset(objs[i], pattern(i), obj_size);
    }

    /* free every other object, alternating between the two halves of
     * objs so the free list hops from one slab to the other.
     * invariant: n_dead objects are dead, the rest hold their pattern */
    size_t n_dead = 0;
    for (size_t k = 0; k < g.slots; k += 2) {
        pool_free(p, objs[k]);
        dead[k] = true;
        pool_free(p, objs[k + g.slots]);
        dead[k + g.slots] = true;
        n_dead += 2;
        expect_stats(p, 2, n - n_dead, n_dead);
        check_pool(p, obj_size);
    }
    assert(fault_alloc_total() == total);

    /* the link was written inside the dead slots only.
     * invariant: survivors before i are intact */
    for (size_t i = 0; i < n; i++) {
        const unsigned char *b = objs[i];
        for (size_t j = 0; !dead[i] && j < obj_size; j++) {
            assert(b[j] == pattern(i));
        }
    }

    /* invariant: each object taken so far was a dead one, taken once */
    for (size_t i = 0; i < n_dead; i++) {
        void  *o = pool_alloc(p);
        size_t j = 0;

        /* invariant: o is none of objs[0..j-1] */
        while (j < n && objs[j] != o) {
            j++;
        }
        assert(j < n);
        assert(dead[j]);
        dead[j] = false;
        memset(o, pattern(j), obj_size);
        check_pool(p, obj_size);
    }
    expect_stats(p, 2, n, 0);
    assert(fault_alloc_total() == total);
    check_patterns(objs, n, obj_size);

    /* destroy with a mix of live and dead objects */
    pool_free(p, objs[0]);
    pool_free(p, objs[n - 1]);
    expect_stats(p, 2, n - 2, 2);
    check_pool(p, obj_size);
    pool_destroy(p);
    assert(fault_alloc_live() == live_before);
}

static void test_pool_free(void)
{
    test_pool_free_size(48);
    test_pool_free_size(49);
    test_pool_free_size(1);
    test_pool_free_size(4080);
}

/* Reads a dead slot through its stale pointer, the way a buggy caller
 * would: the link must be `link`, every byte after it must be poison. */
static void check_dead_slot(const void *obj, const void *link, size_t stride)
{
    const unsigned char *b = obj;
    void *found;

    memcpy(&found, obj, sizeof found);
    assert(found == link);

    /* invariant: bytes sizeof found .. j-1 are poison */
    for (size_t j = sizeof found; j < stride; j++) {
        assert(b[j] == POOL_POISON);
    }
}

static void test_pool_poison_size(size_t obj_size)
{
    struct pool_geom g;
    void *objs[2 * POOL_TEST_CAP] = { NULL };
    long  live_before = fault_alloc_live();

    assert(pool_geometry(obj_size, &g) == 0);
    assert(g.slots <= POOL_TEST_CAP);
    assert(g.stride > sizeof(void *)); /* there is something to poison */
    const size_t n = 2 * g.slots;

    rb_pool_t *p = pool_create(obj_size);
    assert(p != NULL);

    /* two slabs exactly full, so the free list starts empty.
     * invariant: objects before i are live and hold their pattern */
    for (size_t i = 0; i < n; i++) {
        objs[i] = pool_alloc(p);
        assert(objs[i] != NULL);
        memset(objs[i], pattern(i), obj_size);
    }
    expect_stats(p, 2, n, 0);

    void *a = objs[0];
    void *b = objs[n - 1];
    /* the two we free must not look poisoned before they die */
    assert(pattern(0) != POOL_POISON && pattern(n - 1) != POOL_POISON);

    pool_free(p, a);
    check_dead_slot(a, NULL, g.stride); /* list was empty: link is NULL */
    check_pool(p, obj_size);

    if (b != a) { /* one slot per slab at the largest size: a and b differ */
        pool_free(p, b);
        check_dead_slot(b, a, g.stride);    /* b links to the old head */
        check_dead_slot(a, NULL, g.stride); /* and a is undisturbed */
        check_pool(p, obj_size);
    }

    /* poisoning stayed inside the dead slots.
     * invariant: survivors before i hold their full pattern */
    for (size_t i = 1; i + 1 < n; i++) {
        const unsigned char *s = objs[i];
        for (size_t j = 0; j < obj_size; j++) {
            assert(s[j] == pattern(i));
        }
    }

    /* the pool still works: last in, first out */
    if (b != a) {
        assert(pool_alloc(p) == b);
    }
    assert(pool_alloc(p) == a);
    expect_stats(p, 2, n, 0);
    check_pool(p, obj_size);

    pool_destroy(p);
    assert(fault_alloc_live() == live_before);
}

static void test_pool_poison(void)
{
    test_pool_poison_size(48);
    test_pool_poison_size(49);
    test_pool_poison_size(1);
    test_pool_poison_size(4080);
}

enum { POOL_HELD = 400, POOL_FIRST = 300, POOL_CHURN = 150 };

/* One sweep run: the pool plus the test's own model of what it must hold. */
typedef struct {
    rb_pool_t *p;
    size_t     obj_size;
    size_t     slots;            /* per slab, from pool_geometry */
    size_t     slabs;            /* model: grows that succeeded */
    size_t     live;             /* model: non-NULL entries of held */
    void      *held[POOL_HELD];  /* NULL = not currently allocated */
} pool_run_t;

/* the pool's three numbers must equal the model's, not merely add up */
static void pool_run_check(const pool_run_t *r)
{
    expect_stats(r->p, r->slabs, r->live, r->slabs * r->slots - r->live);
    check_pool(r->p, r->obj_size);
}

static void pool_run_check_obj(const pool_run_t *r, size_t i)
{
    const unsigned char *b = r->held[i];

    /* invariant: bytes before j still hold object i's pattern */
    for (size_t j = 0; j < r->obj_size; j++) {
        assert(b[j] == pattern(i));
    }
}

/* a slot handed to two holders would show up here as a wrecked pattern */
static void pool_run_check_live(const pool_run_t *r)
{
    size_t n = 0;

    /* invariant: n held objects before i are intact */
    for (size_t i = 0; i < POOL_HELD; i++) {
        if (r->held[i]) {
            pool_run_check_obj(r, i);
            n++;
        }
    }
    assert(n == r->live);
}

/* Allocate into held[i]. Returns whether this call hit the fault. */
static bool pool_run_alloc(pool_run_t *r, size_t i)
{
    size_t free_objs;

    assert(r->held[i] == NULL);
    pool_stats(r->p, NULL, NULL, &free_objs);

    long  before = fault_alloc_total();
    void *o      = pool_alloc(r->p);
    bool  hit    = fault_hit(before);

    if (free_objs > 0) {
        /* a pop: no rb_malloc, so it cannot fail */
        assert(fault_alloc_total() == before);
        assert(o != NULL);
    } else {
        /* a grow: exactly one rb_malloc, and it is the only way to fail */
        assert(fault_alloc_total() == before + 1);
        if (hit) {
            assert(o == NULL);
        } else {
            assert(o != NULL);
            r->slabs++;
        }
    }

    if (o) {
        memset(o, pattern(i), r->obj_size);
        r->held[i] = o;
        r->live++;
    }
    pool_run_check(r); /* on a failed grow: model untouched, so must the pool be */
    return hit;
}

/* Free held[i], which may be absent if its alloc was the one that failed. */
static void pool_run_free(pool_run_t *r, size_t i)
{
    if (!r->held[i]) {
        return;
    }
    pool_run_check_obj(r, i);

    long before = fault_alloc_total();
    pool_free(r->p, r->held[i]);
    assert(fault_alloc_total() == before);

    r->held[i] = NULL;
    r->live--;
    pool_run_check(r);
}

/* One run: allocate POOL_FIRST, free POOL_CHURN of them scrambled, allocate
 * those back, free everything scrambled, allocate POOL_HELD, destroy with
 * all of them live; the n-th allocation fails. 37 shares no factor with
 * POOL_FIRST or POOL_HELD, so each scramble visits every index once.
 * Returns whether the fault was hit. */
static bool pool_scenario(size_t obj_size, long n)
{
    bool hit = false;
    long live_before = fault_alloc_live();
    struct pool_geom g;
    pool_run_t r = { .obj_size = obj_size };

    assert(pool_geometry(obj_size, &g) == 0);
    r.slots = g.slots;

    fire_at = fault_alloc_total() + n;
    fault_alloc_arm(n);

    long before = fault_alloc_total();
    r.p = pool_create(obj_size);
    if (fault_hit(before)) {
        assert(r.p == NULL);
        fault_alloc_disarm();
        assert(fault_alloc_live() == live_before);
        return true;
    }
    assert(r.p != NULL);
    pool_run_check(&r);

    /* invariant: held[0..i-1] are allocated, except one if it faulted */
    for (size_t i = 0; i < POOL_FIRST; i++) {
        hit |= pool_run_alloc(&r, i);
    }
    pool_run_check_live(&r);

    /* invariant: the first i scrambled indices are not held */
    for (size_t i = 0; i < POOL_CHURN; i++) {
        pool_run_free(&r, (i * 37) % POOL_FIRST);
    }
    pool_run_check_live(&r);

    /* invariant: the first i scrambled indices are held again */
    for (size_t i = 0; i < POOL_CHURN; i++) {
        hit |= pool_run_alloc(&r, (i * 37) % POOL_FIRST);
    }
    pool_run_check_live(&r);

    /* invariant: the first i scrambled indices are not held */
    for (size_t i = 0; i < POOL_HELD; i++) {
        pool_run_free(&r, (i * 37) % POOL_HELD);
    }
    assert(r.live == 0);

    /* invariant: held[0..i-1] are allocated, except one if it faulted */
    for (size_t i = 0; i < POOL_HELD; i++) {
        hit |= pool_run_alloc(&r, i);
    }
    pool_run_check_live(&r);

    fault_alloc_disarm();
    pool_destroy(r.p); /* every object still live */
    assert(fault_alloc_live() == live_before);
    return hit;
}

/* expect_faulted: one fault point for the pool plus one per slab a clean
 * run needs, so a change in how often the pool allocates fails here */
static void test_pool_sweep_size(size_t obj_size, long expect_faulted)
{
    long n = 1;

    /* invariant: every run with a fault index below n hit its fault */
    while (pool_scenario(obj_size, n)) {
        printf("pool sweep size=%zu n=%ld: faulted, ok\n", obj_size, n);
        fflush(stdout); /* keep the line if a later assert aborts */
        n++;
        assert(n < MAX_SWEEP);
    }

    printf("pool fault sweep passed: size=%zu, %ld faulted runs, clean at n=%ld\n",
           obj_size, n - 1, n);
    fflush(stdout); /* show the count even if the next assert aborts */
    assert(n - 1 == expect_faulted);
}

static void test_pool_sweep(void)
{
    test_pool_sweep_size(48, 6);     /* pool + 5 slabs of 85 for 400 */
    test_pool_sweep_size(49, 8);     /* pool + 7 slabs of 63 */
    test_pool_sweep_size(1, 3);      /* pool + 2 slabs of 255 */
    test_pool_sweep_size(4080, 401); /* pool + one slab per object */
}

/* The whole Assignment 1 + Mutation 1 battery, on whichever kind of tree
 * create_tree makes. */
static void test_tree_battery(long expect_faulted)
{
    free_count = 0; /* the checks below count from zero */

    rb_destroy(NULL); /* must not crash */

    rbtree_t *t = create_tree(counting_free);
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
    rbtree_t *t2 = create_tree(NULL);
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
    rbtree_t *t3 = create_tree(free);
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
    test_fault_sweep(expect_faulted);
}

/* The battery alone cannot tell a pooled tree from a plain one, so pin the
 * difference: a pooled insert takes its node from a slab, and only calls
 * rb_malloc for the key copy unless a new slab is needed. */
static void test_pooled_uses_pool(void)
{
    struct pool_geom g;
    static int v;
    char key[KEY_CAP];
    long live_before = fault_alloc_live();

    /* 48 = sizeof(struct rb_node), which the test cannot name directly */
    assert(pool_geometry(48, &g) == 0);

    rbtree_t *t = rb_create_pooled(NULL);
    assert(t != NULL);
    assert(fault_alloc_live() == live_before + 2); /* tree + pool, no slab */

    /* two slabs' worth of new keys.
     * invariant: the i keys so far cost one rb_malloc each, plus one per slab */
    for (size_t i = 0; i < 2 * g.slots; i++) {
        long before = fault_alloc_total();

        make_key(key, (int)i);
        assert(rb_insert(t, key, &v) == 0);
        assert(fault_alloc_total() == before + (i % g.slots == 0 ? 2 : 1));
    }
    assert(rb_validate(t) == 0);

    /* a deleted node's slot is reused: still one rb_malloc, for the key */
    make_key(key, 0);
    assert(rb_delete(t, key) == 0);
    long before = fault_alloc_total();
    assert(rb_insert(t, key, &v) == 0);
    assert(fault_alloc_total() == before + 1);
    assert(rb_validate(t) == 0);

    rb_destroy(t);
    assert(fault_alloc_live() == live_before);
}

/* A pooled insert whose key copy fails must hand its node back to the
 * pool. A lost slot leaks no memory (rb_destroy frees the slab), so only
 * counting rb_malloc calls on the inserts that follow can see it. */
static void test_pooled_failed_insert(void)
{
    struct pool_geom g;
    static int v;
    char key[KEY_CAP];
    long live_before = fault_alloc_live();

    assert(pool_geometry(48, &g) == 0);

    rbtree_t *t = rb_create_pooled(NULL);
    assert(t != NULL);

    /* invariant: i keys are in the tree, all in the first slab */
    for (size_t i = 0; i < g.slots; i++) {
        make_key(key, (int)i);
        assert(rb_insert(t, key, &v) == 0);
    }

    /* one free slot; the node comes from it, then the key copy fails */
    make_key(key, 0);
    assert(rb_delete(t, key) == 0);
    fault_alloc_arm(1);
    assert(rb_insert(t, key, &v) == -1);
    fault_alloc_disarm();
    assert(rb_size(t) == g.slots - 1);
    assert(rb_find(t, key) == NULL);
    assert(rb_validate(t) == 0);

    /* the slot is still there: the retry needs no new slab */
    long before = fault_alloc_total();
    assert(rb_insert(t, key, &v) == 0);
    assert(fault_alloc_total() == before + 1);

    /* slab full: the new slab arrives, then the key copy fails */
    make_key(key, (int)g.slots);
    before = fault_alloc_total();
    fault_alloc_arm(2);
    assert(rb_insert(t, key, &v) == -1);
    fault_alloc_disarm();
    assert(fault_alloc_total() == before + 2);
    assert(rb_size(t) == g.slots);
    assert(rb_find(t, key) == NULL);
    assert(rb_validate(t) == 0);

    /* that slab was kept with every slot free: a slab's worth of new keys
     * costs one rb_malloc each.
     * invariant: i of the second slab's slots are in use */
    for (size_t i = 0; i < g.slots; i++) {
        make_key(key, (int)(g.slots + i));
        before = fault_alloc_total();
        assert(rb_insert(t, key, &v) == 0);
        assert(fault_alloc_total() == before + 1);
    }
    assert(rb_validate(t) == 0);

    rb_destroy(t);
    assert(fault_alloc_live() == live_before);
}

int main(void)
{
    printf("== tree battery: rb_create\n");
    create_tree = rb_create;
    test_tree_battery(601); /* tree + 2 x 300 new-key inserts */

    test_pool_geometry();
    test_pool_empty();
    test_pool_alloc();
    test_pool_free();
    test_pool_poison();
    test_pool_sweep();
    test_pooled_uses_pool();
    test_pooled_failed_insert();

    printf("== tree battery: rb_create_pooled\n");
    create_tree = rb_create_pooled;
    test_tree_battery(305); /* tree + pool + 300 key copies + 3 slabs */

    printf("all tests passed\n");
    return 0;
}
