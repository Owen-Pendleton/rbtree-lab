#ifndef POOL_H
#define POOL_H
#include <stddef.h>

typedef struct rb_pool rb_pool_t;
/* slabs are 4096 bytes */
rb_pool_t *pool_create(size_t obj_size);
/* O(1), from a free list */
void *pool_alloc(rb_pool_t *p);
/* O(1), returns to the free list */
void pool_free(rb_pool_t *p, void *obj);
void pool_stats(const rb_pool_t *p,
size_t *slabs, size_t *live, size_t *free_objs);
/* releases all slabs */
void pool_destroy(rb_pool_t *p);

#define POOL_SLAB_SIZE 4096
/* debug builds fill a dead slot with this, after the free-list link */
#define POOL_POISON 0xDD

/* Layout of one slab for a given object size. All fields are in bytes
 * except slots, which is a count. */
struct pool_geom {
    size_t stride;    /* bytes from one slot to the next */
    size_t slots;     /* slots per slab */
    size_t first_off; /* slab base to slot 0 (past the in-band header) */
    size_t link_off;  /* slot start to the free-list pointer */
};

/* Returns 0 and fills *g, or -1 if obj_size is 0 or too big for one slot
 * per slab (*g untouched). Allocates nothing. */
int pool_geometry(size_t obj_size, struct pool_geom *g);

#endif
