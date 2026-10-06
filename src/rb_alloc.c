#include "../tests/fault_alloc.h"

#include <stdlib.h>

void *rb_malloc(size_t n)
{
    return malloc(n);
}

void rb_free(void *p)
{
    free(p);
}
