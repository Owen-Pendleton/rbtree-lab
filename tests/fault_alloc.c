#include "fault_alloc.h"

#include <stdlib.h>

static long total = 0;     /* every rb_malloc call so far, failed or not */
static long countdown = 0; /* calls left until the fault; 0 = disarmed */

void *rb_malloc(size_t n)
{
    total++;
    if (countdown > 0 && --countdown == 0) {
        return NULL;
    }
    return malloc(n);
}

void rb_free(void *p)
{
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
