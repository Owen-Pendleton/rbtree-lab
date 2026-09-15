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

/* One recorded operation decision. Logging the decision (not just "an op
 * happened") means replaying ops[0..len) reproduces a run deterministically
 * with no RNG involved, which is what makes shrinking possible. */
struct op {
    bool do_insert;
    long num;
};

/* Replays ops[0..len) against a fresh tree/model and reports whether the
 * run ends up broken: a per-step rc/size mismatch, or a final rb_validate
 * failure. Used only by ddmin() to test candidate reductions. It doesn't
 * replay the foreach/find checks main() runs at the very end, so a failure
 * first detected by one of those two may not shrink as tightly - the
 * reduction is still attempted, since such bugs often also show up as a
 * validate failure once the sequence is short enough. */
static bool replay_is_broken(const struct op *ops, long len, long n)
{
    bool *present = calloc((size_t)(n * 2), sizeof *present);
    int  *value   = malloc((size_t)(n * 2) * sizeof *value);
    rbtree_t *t   = (present && value) ? rb_create(free) : NULL;
    if (!present || !value || !t) {
        free(present);
        free(value);
        rb_destroy(t);
        return false; /* can't tell; ddmin treats this reduction as unhelpful */
    }

    bool broken      = false;
    long model_count = 0;

    /* invariant: replays exactly the logged decisions in order; once one
     * step disagrees with the model, this (shorter) candidate is already
     * broken and there's no need to keep replaying it */
    for (long i = 0; i < len && !broken; i++) {
        char key[32];
        snprintf(key, sizeof key, "k%ld", ops[i].num);

        if (ops[i].do_insert) {
            int *v = malloc(sizeof *v);
            if (!v || rb_insert(t, key, v) != 0) {
                free(v);
                broken = true;
                break;
            }
            if (!present[ops[i].num]) {
                model_count++;
            }
            present[ops[i].num] = true;
            value[ops[i].num]   = (int)i;
        } else {
            int  rc              = rb_delete(t, key);
            bool expected_present = present[ops[i].num];
            if ((rc == 0) != expected_present) {
                broken = true;
                break;
            }
            if (rc == 0) {
                present[ops[i].num] = false;
                model_count--;
            }
        }

        if (rb_size(t) != (size_t)model_count) {
            broken = true;
        }
    }
    if (!broken && rb_validate(t) != 0) {
        broken = true;
    }

    rb_destroy(t);
    free(present);
    free(value);
    return broken;
}

/* Classic delta-debugging (Zeller's ddmin): repeatedly try dropping a
 * contiguous chunk of the current candidate and keep the drop if the
 * failure still reproduces, widening the chunk count whenever a full pass
 * finds nothing droppable, until no single operation can be removed.
 * Shrinks ops[] in place and returns the new (smaller-or-equal) length. */
static long ddmin(struct op *ops, long len, long n)
{
    long chunks = 2;
    while (len >= 2) {
        long chunk_size = (len + chunks - 1) / chunks;
        bool reduced    = false;

        /* invariant: each pass tries removing one chunk-sized block at a
         * time; c walks the chunks left to right over the current ops[] */
        for (long c = 0; c < chunks; c++) {
            long start = c * chunk_size;
            long end   = (start + chunk_size < len) ? start + chunk_size : len;
            if (start >= end) {
                continue;
            }

            long kept_len = len - (end - start);
            struct op *kept = malloc((size_t)kept_len * sizeof *kept);
            if (!kept) {
                return len; /* out of memory mid-shrink: report what we have */
            }
            memcpy(kept, ops, (size_t)start * sizeof *kept);
            memcpy(kept + start, ops + end, (size_t)(len - end) * sizeof *kept);

            if (kept_len > 0 && replay_is_broken(kept, kept_len, n)) {
                memcpy(ops, kept, (size_t)kept_len * sizeof *kept);
                len     = kept_len;
                chunks  = (chunks - 1 > 2) ? chunks - 1 : 2;
                reduced = true;
                free(kept);
                break;
            }
            free(kept);
        }

        if (!reduced) {
            if (chunks >= len) {
                break;
            }
            chunks = (chunks * 2 < len) ? chunks * 2 : len;
        }
    }
    return len;
}

static void print_shrunk(const struct op *ops, long len)
{
    fprintf(stderr, "shrunk repro (%ld ops):\n", len);
    for (long i = 0; i < len; i++) {
        fprintf(stderr, "  %s k%ld\n", ops[i].do_insert ? "insert" : "delete", ops[i].num);
    }
}

int main(int argc, char **argv)
{
    long n = (argc > 1) ? strtol(argv[1], NULL, 10) : 1000;
    if (n <= 0) {
        n = 1000;
    }

    /* fixed default so a bare `./fuzz N` run is reproducible without an
     * explicit seed argument; fuzz.c never called srand() before this */
    long seed = (argc > 2) ? strtol(argv[2], NULL, 10) : 1337;
    srand((unsigned)seed);

    struct model model = {0};
    model.present = calloc((size_t)(n * 2), sizeof *model.present);
    model.value   = malloc((size_t)(n * 2) * sizeof *model.value);
    struct op *ops = malloc((size_t)n * sizeof *ops);
    if (!model.present || !model.value || !ops) {
        fprintf(stderr, "allocation failed\n");
        free(model.present);
        free(model.value);
        free(ops);
        return 1;
    }

    rbtree_t *t = rb_create(free);
    if (!t) {
        fprintf(stderr, "rb_create failed\n");
        free(model.present);
        free(model.value);
        free(ops);
        return 1;
    }

    /* invariant: keys are drawn from a space twice the operation count, so
     * both rb_insert's overwrite path and rb_delete's absent-key path fire
     * often; ops are biased toward insert so the tree has room to grow
     * before delete gets much chance to shrink it back down. model_count is
     * maintained incrementally so it stays cheap to compare against
     * rb_size(t) after every single operation, not just at the end. */
    long model_count = 0;

    /* rb_validate walks the whole tree, so calling it every iteration would
     * make this loop O(n^2); check at fixed checkpoints instead. argv[3]
     * overrides the default spacing (e.g. "100" for a denser stress run);
     * make test/asan/memcheck don't pass it, so they keep today's spacing. */
    long default_interval  = (n / 200 < 1) ? 1 : n / 200;
    long validate_interval = (argc > 3) ? strtol(argv[3], NULL, 10) : default_interval;
    if (validate_interval < 1) {
        validate_interval = 1;
    }

    long        fail_at  = -1; /* >= 0 once something breaks: shrink and report */
    const char *fail_msg = NULL;

    for (long i = 0; i < n; i++) {
        long num = rand() % (n * 2);
        char key[32];
        snprintf(key, sizeof key, "k%ld", num);

        bool do_insert   = (rand() % 100) < 60;
        ops[i].do_insert = do_insert;
        ops[i].num       = num;

        if (do_insert) {
            int *value = malloc(sizeof *value);
            if (!value) {
                fprintf(stderr, "malloc failed at i=%ld\n", i);
                rb_destroy(t);
                free(model.present);
                free(model.value);
                free(ops);
                return 1;
            }
            *value = (int)i;

            if (rb_insert(t, key, value) != 0) {
                fprintf(stderr, "rb_insert failed at i=%ld\n", i);
                free(value);
                rb_destroy(t);
                free(model.present);
                free(model.value);
                free(ops);
                return 1;
            }

            if (!model.present[num]) {
                model_count++;
            }
            model.present[num] = true;
            model.value[num]   = (int)i;
        } else {
            int  rc               = rb_delete(t, key);
            bool expected_present = model.present[num];

            if ((rc == 0) != expected_present) {
                fail_at  = i;
                fail_msg = "rb_delete mismatch";
                break;
            }
            if (rc == 0) {
                model.present[num] = false;
                model_count--;
            }
        }

        if (i % validate_interval == 0 && rb_validate(t) != 0) {
            fail_at  = i;
            fail_msg = "rb_validate failed mid-run";
            break;
        }
        if (rb_size(t) != (size_t)model_count) {
            fail_at  = i;
            fail_msg = "rb_size mismatch";
            break;
        }
    }

    if (fail_at < 0 && rb_validate(t) != 0) {
        fail_at  = n - 1;
        fail_msg = "rb_validate failed at final state";
    }

    if (fail_at < 0) {
        /* invariant: recount from scratch as a cross-check against
         * model_count, which was maintained incrementally through every
         * insert/delete above */
        long recount = 0;
        for (long num = 0; num < n * 2; num++) {
            if (model.present[num]) {
                recount++;
            }
        }
        if (recount != model_count) {
            fail_at  = n - 1;
            fail_msg = "model bookkeeping mismatch";
        }
    }

    if (fail_at < 0) {
        struct foreach_ctx ctx = {.model = &model, .ok = true};
        rb_foreach(t, check_cb, &ctx);
        if (!ctx.ok || ctx.count != model_count || (size_t)ctx.count != rb_size(t)) {
            fail_at  = n - 1;
            fail_msg = "rb_foreach mismatch";
        }
    }

    if (fail_at < 0) {
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
                fail_at  = n - 1;
                fail_msg = "rb_find mismatch";
                break;
            }
        }
    }

    if (fail_at >= 0) {
        fprintf(stderr, "%s (seed=%ld, %ld ops logged); shrinking...\n",
                fail_msg, seed, fail_at + 1);
        long shrunk_len = ddmin(ops, fail_at + 1, n);
        print_shrunk(ops, shrunk_len);
        rb_destroy(t);
        free(model.present);
        free(model.value);
        free(ops);
        return 1;
    }

    rb_destroy(t);
    free(model.present);
    free(model.value);
    free(ops);
    printf("fuzz ok: %ld operations (seed=%ld)\n", n, seed);
    return 0;
}
