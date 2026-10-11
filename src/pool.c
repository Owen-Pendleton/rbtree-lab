#include "pool.h"

#include <stddef.h>

/* In-band slab header: sits at offset 0 of every 4096-byte slab. */
struct pool_slab {
    struct pool_slab *next;
};

/* What a dead slot holds: the free-list link, at offset 0 of the slot. */
struct pool_slot {
    struct pool_slot *next;
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
