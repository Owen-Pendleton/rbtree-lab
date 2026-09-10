#include "rbtree.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reference model: keys are always "k<num>" with num in [0, 2n), so the
 * cheapest trustworthy oracle is a pair of arrays indexed directly by num,
 * updated in lockstep with every rb_insert. */
struct model {
    bool *present;
    int  *value;
};

struct foreach_ctx {
    const struct model *model;
    long                count;
    bool                ok;
    const char         *last_key; /* NULL before the first callback */
};

static void check_cb(const char *key, void *value, void *ctx_)
{
    struct foreach_ctx *ctx = ctx_;
    ctx->count++;

    if (ctx->last_key != NULL && strcmp(ctx->last_key, key) >= 0) {
        fprintf(stderr, "rb_foreach: keys out of order at %s\n", key);
        ctx->ok = false;
    }
    ctx->last_key = key;

    long num = strtol(key + 1, NULL, 10); /* skip leading 'k' */
    if (!ctx->model->present[num] || *(int *)value != ctx->model->value[num]) {
        fprintf(stderr, "rb_foreach: mismatch at %s\n", key);
        ctx->ok = false;
    }
}

int main(int argc, char **argv)
{
    long n = (argc > 1) ? strtol(argv[1], NULL, 10) : 1000;
    if (n <= 0) {
        n = 1000;
    }

    struct model model = {0};
    model.present = calloc((size_t)(n * 2), sizeof *model.present);
    model.value   = malloc((size_t)(n * 2) * sizeof *model.value);
    if (!model.present || !model.value) {
        fprintf(stderr, "model allocation failed\n");
        free(model.present);
        free(model.value);
        return 1;
    }

    rbtree_t *t = rb_create(free);
    if (!t) {
        fprintf(stderr, "rb_create failed\n");
        free(model.present);
        free(model.value);
        return 1;
    }

    /* invariant: keys are drawn from a space twice the operation count, so
     * both rb_insert's overwrite path and rb_delete's absent-key path fire
     * often; ops are biased toward insert so the tree has room to grow
     * before delete gets much chance to shrink it back down. model_count is
     * maintained incrementally so it stays cheap to compare against
     * rb_size(t) after every single operation, not just at the end. */
    long model_count      = 0;
    long validate_interval = (n / 200 < 1) ? 1 : n / 200;
    for (long i = 0; i < n; i++) {
        long num = rand() % (n * 2);
        char key[32];
        snprintf(key, sizeof key, "k%ld", num);

        bool do_insert = (rand() % 100) < 60;

        if (do_insert) {
            int *value = malloc(sizeof *value);
            if (!value) {
                fprintf(stderr, "malloc failed at i=%ld\n", i);
                rb_destroy(t);
                free(model.present);
                free(model.value);
                return 1;
            }
            *value = (int)i;

            if (rb_insert(t, key, value) != 0) {
                fprintf(stderr, "rb_insert failed at i=%ld\n", i);
                free(value);
                rb_destroy(t);
                free(model.present);
                free(model.value);
                return 1;
            }

            if (!model.present[num]) {
                model_count++;
            }
            model.present[num] = true;
            model.value[num]   = (int)i;
        } else {
            int  rc              = rb_delete(t, key);
            bool expected_present = model.present[num];

            if ((rc == 0) != expected_present) {
                fprintf(stderr,
                        "rb_delete mismatch at i=%ld, %s: rc=%d expected_present=%d\n",
                        i, key, rc, expected_present);
                rb_destroy(t);
                free(model.present);
                free(model.value);
                return 1;
            }
            if (rc == 0) {
                model.present[num] = false;
                model_count--;
            }
        }

        /* rb_validate walks the whole tree, so calling it every iteration
         * would make this loop O(n^2); check at a fixed number of
         * checkpoints spread across the run instead, trading exact
         * localization for staying linear in n at fuzz scale */
        if (i % validate_interval == 0 && rb_validate(t) != 0) {
            fprintf(stderr, "rb_validate failed at i=%ld: invariants broken\n", i);
            rb_destroy(t);
            free(model.present);
            free(model.value);
            return 1;
        }
        if (rb_size(t) != (size_t)model_count) {
            fprintf(stderr, "rb_size mismatch at i=%ld: rb_size=%zu model=%ld\n",
                    i, rb_size(t), model_count);
            rb_destroy(t);
            free(model.present);
            free(model.value);
            return 1;
        }
    }

    if (rb_validate(t) != 0) {
        fprintf(stderr, "rb_validate failed: invariants broken at final state\n");
        rb_destroy(t);
        free(model.present);
        free(model.value);
        return 1;
    }

    /* invariant: recount from scratch as a cross-check against model_count,
     * which was maintained incrementally through every insert/delete above */
    long recount = 0;
    for (long num = 0; num < n * 2; num++) {
        if (model.present[num]) {
            recount++;
        }
    }
    if (recount != model_count) {
        fprintf(stderr, "model bookkeeping mismatch: recount=%ld running=%ld\n",
                recount, model_count);
        rb_destroy(t);
        free(model.present);
        free(model.value);
        return 1;
    }

    struct foreach_ctx ctx = {.model = &model, .ok = true};
    rb_foreach(t, check_cb, &ctx);

    if (!ctx.ok || ctx.count != model_count || (size_t)ctx.count != rb_size(t)) {
        fprintf(stderr,
                "rb_foreach mismatch: foreach=%ld model=%ld rb_size=%zu ok=%d\n",
                ctx.count, model_count, rb_size(t), ctx.ok);
        rb_destroy(t);
        free(model.present);
        free(model.value);
        return 1;
    }

    /* invariant: i walks a sample of the same key space; every hit/miss
     * must match the reference model exactly, not just avoid crashing */
    for (long i = 0; i < n; i += (n / 10 < 1 ? 1 : n / 10)) {
        long num = rand() % (n * 2);
        char key[32];
        snprintf(key, sizeof key, "k%ld", num);

        void *found = rb_find(t, key);
        bool  mismatch = model.present[num]
                              ? (!found || *(int *)found != model.value[num])
                              : (found != NULL);
        if (mismatch) {
            fprintf(stderr, "rb_find mismatch at %s\n", key);
            rb_destroy(t);
            free(model.present);
            free(model.value);
            return 1;
        }
    }

    rb_destroy(t);
    free(model.present);
    free(model.value);
    printf("fuzz ok: %ld operations\n", n);
    return 0;
}
