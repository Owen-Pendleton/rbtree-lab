#include "fault_alloc.h"

#include <stdlib.h>

static long total = 0;     /* every rb_malloc call so far, failed or not */
static long countdown = 0; /* calls left until the fault; 0 = disarmed */
static long live = 0;      /* successful rb_malloc calls not yet rb_free'd */

void *rb_malloc(size_t n)
{
    total++;
    if (countdown > 0 && --countdown == 0) {
        return NULL;
    }
    void *p = malloc(n);
    if (p) {
        live++;
    }
    return p;
}

void rb_free(void *p)
{
    if (p) {
        live--;
    }
    free(p);
}

void fault_alloc_arm(long n)
{
    countdown = n;
}

void fault_alloc_disarm(void)
{
    countdown = 0;
}

long fault_alloc_total(void)
{
    return total;
}

long fault_alloc_live(void)
{
    return live;
}
