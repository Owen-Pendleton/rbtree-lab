#include "pool.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Allocation seam (same declarations as tests/fault_alloc.h), repeated
 * here so src/ builds without the tests/ directory. */
void *rb_malloc(size_t n);
void rb_free(void *p);

/* In-band slab header: sits at offset 0 of every 4096-byte slab. */
struct pool_slab {
    struct pool_slab *next;
};

/* What a dead slot holds: the free-list link, at offset 0 of the slot. */
struct pool_slot {
    struct pool_slot *next;
};

struct rb_pool {
    struct pool_slab *slabs;   /* every slab, newest first; destroy walks this */
    struct pool_slot *free;    /* head of the free list, NULL when empty */
    size_t            n_slabs;
    size_t            live;    /* handed out, not yet returned */
    size_t            n_free;  /* length of the free list */
    struct pool_geom  geom;    /* computed once, never changes */
};

/* The slab base comes from rb_malloc, so it is aligned for max_align_t.
 * If the slab size is a multiple of that too, the room left after the
 * header is as well, and a stride that passes the range check fits. */
static_assert(POOL_SLAB_SIZE % alignof(max_align_t) == 0);

/* Smallest multiple of a that is >= n. Caller keeps n small enough that
 * n + a cannot overflow. */
static size_t round_up(size_t n, size_t a)
{
    return (n + a - 1) / a * a;
}

int pool_geometry(size_t obj_size, struct pool_geom *g)
{
    const size_t align     = alignof(max_align_t);
    const size_t first_off = round_up(sizeof(struct pool_slab), align);
    const size_t room      = POOL_SLAB_SIZE - first_off;

    /* checked before rounding, so a huge obj_size cannot overflow */
    if (obj_size == 0 || obj_size > room) {
        return -1;
    }

    /* a dead slot must be able to hold the free-list link */
    size_t need = obj_size;
    if (need < sizeof(struct pool_slot)) {
        need = sizeof(struct pool_slot);
    }

    g->stride    = round_up(need, align);
    g->slots     = room / g->stride;
    g->first_off = first_off;
    g->link_off  = offsetof(struct pool_slot, next);
    return 0;
}

rb_pool_t *pool_create(size_t obj_size)
{
    struct pool_geom g;

    /* reject a bad size before allocating anything */
    if (pool_geometry(obj_size, &g) != 0) {
        return NULL;
    }

    struct rb_pool *p = rb_malloc(sizeof *p);
    if (!p) {
        return NULL;
    }

    p->slabs   = NULL;
    p->free    = NULL;
    p->n_slabs = 0;
    p->live    = 0;
    p->n_free  = 0;
    p->geom    = g;

    return p;
}

/* Gets one more slab and puts all of its slots on the free list.
 * Returns 0, or -1 if the allocation fails (pool unchanged). */
static int pool_grow(rb_pool_t *p)
{
    const struct pool_geom *g = &p->geom;

    struct pool_slab *s = rb_malloc(POOL_SLAB_SIZE);
    if (!s) {
        return -1;
    }

    s->next  = p->slabs;
    p->slabs = s;
    p->n_slabs++;

    char *base = (char *)s; /* char * so the arithmetic below is in bytes */

    /* invariant: slots i..slots-1 of this slab are on the free list, in
     * ascending address order from the head */
    for (size_t i = g->slots; i > 0; i--) {
        struct pool_slot *slot =
            (struct pool_slot *)(base + g->first_off + (i - 1) * g->stride);
        slot->next = p->free;
        p->free    = slot;
    }
    p->n_free += g->slots;

    return 0;
}

void *pool_alloc(rb_pool_t *p)
{
    if (!p->free && pool_grow(p) != 0) {
        return NULL;
    }

    struct pool_slot *slot = p->free;
    p->free = slot->next;
    p->n_free--;
    p->live++;

    return slot;
}

/* Who owns a slot's bytes.
 * Live (from pool_alloc until pool_free): the caller owns bytes
 * [0, obj_size). The pool neither reads nor writes them.
 * Dead (from pool_free until pool_alloc hands it out again): the pool owns
 * all stride bytes. The free-list link takes the first
 * sizeof(struct pool_slot) bytes, and where link and poison want the same
 * bytes the link wins: the pool cannot work without it. In debug builds
 * every byte after the link is POOL_POISON.
 * Slots of a freshly carved slab are not poisoned, so Valgrind still sees
 * them as uninitialized until the caller writes them. */
void pool_free(rb_pool_t *p, void *obj)
{
    if (!obj) {
        return;
    }

    /* cheap guard only: a double free while other objects are live, or a
     * pointer from elsewhere, is the caller's contract and is not caught */
    assert(p->live > 0);

#ifndef NDEBUG
    /* whole slot first; the link below then overwrites the first bytes */
    memset(obj, POOL_POISON, p->geom.stride);
#endif

    struct pool_slot *slot = obj;
    slot->next = p->free;
    p->free    = slot;
    p->n_free++;
    p->live--;
}

void pool_stats(const rb_pool_t *p,
                size_t *slabs, size_t *live, size_t *free_objs)
{
    if (slabs) {
        *slabs = p->n_slabs;
    }
    if (live) {
        *live = p->live;
    }
    if (free_objs) {
        *free_objs = p->n_free;
    }
}

void pool_destroy(rb_pool_t *p)
{
    if (!p) {
        return;
    }

    struct pool_slab *s = p->slabs;

    /* invariant: every slab before s is freed; s and the rest are intact */
    while (s) {
        struct pool_slab *next = s->next; /* read before s is freed */
        rb_free(s);
        s = next;
    }

    rb_free(p);
}
