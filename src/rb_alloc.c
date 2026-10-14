#include <stddef.h>
#include <stdlib.h>

/* Allocation seam (same declarations as tests/fault_alloc.h), repeated
 * here so src/ builds without the tests/ directory. */
void *rb_malloc(size_t n);
void rb_free(void *p);

void *rb_malloc(size_t n)
{
    return malloc(n);
}

void rb_free(void *p)
{
    free(p);
}
