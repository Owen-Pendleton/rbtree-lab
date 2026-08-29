#include "rbtree.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    long n = (argc > 1) ? strtol(argv[1], NULL, 10) : 1000;
    if (n <= 0) {
        n = 1000;
    }

    rbtree_t *t = rb_create(free);
    if (!t) {
        fprintf(stderr, "rb_create failed\n");
        return 1;
    }

    /* invariant: keys are drawn from a space twice the insert count, so
     * collisions (and thus rb_insert's overwrite path) happen often */
    for (long i = 0; i < n; i++) {
        char key[32];
        snprintf(key, sizeof key, "k%ld", rand() % (n * 2));

        int *value = malloc(sizeof *value);
        if (!value) {
            fprintf(stderr, "malloc failed at i=%ld\n", i);
            rb_destroy(t);
            return 1;
        }
        *value = (int)i;

        if (rb_insert(t, key, value) != 0) {
            fprintf(stderr, "rb_insert failed at i=%ld\n", i);
            free(value);
            rb_destroy(t);
            return 1;
        }
    }

    if (rb_validate(t) != 0) {
        fprintf(stderr, "rb_validate failed: ordering broken\n");
        rb_destroy(t);
        return 1;
    }

    /* invariant: i walks a sample of the same key space; hits and misses
     * against the tree must both be handled without crashing */
    for (long i = 0; i < n; i += (n / 10 < 1 ? 1 : n / 10)) {
        char key[32];
        snprintf(key, sizeof key, "k%ld", rand() % (n * 2));
        (void)rb_find(t, key);
    }

    rb_destroy(t);
    printf("fuzz ok: %ld operations\n", n);
    return 0;
}
