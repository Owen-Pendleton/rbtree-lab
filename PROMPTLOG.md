# rbtree-lab

Log of sessions working with Claude Code on this assignment: what was asked,
what got implemented, and the reasoning/decisions behind it.

## 2026-08-27 — Evening 1

**Why sentinel-NIL over `NULL` leaves:** using `NULL` to represent leaves
leaves room for error during rebalancing. With `NULL` leaves, walking and
rotating means tracking `x` and `x->parent` as a separate pair of
variables — `x_parent` has to be manually updated by hand every single time
`x` moves up the tree or a rotation happens, since `NULL` itself can't carry
a parent pointer. That's more places for a bookkeeping mistake to slip in,
and it's harder to check step-by-step against a reference
implementation/table. A real sentinel node has its own `parent` field, so
`x` always knows its own parent without a second variable to keep in sync.

## 2026-08-28 — Evening 2

**Scope:** `rb_create`, `rb_find`, `rb_insert` (without fixup), `rb_validate`'s
BST-ordering check, recursive `rb_destroy`, plus initial unit tests
(`tests/test_rbtree.c`) and a randomized fuzz smoke driver (`tests/fuzz.c`).

**Workflow:** for each function, Claude explained the design first — tied to
the sentinel-NIL layout from evening 1 (`struct rbtree` embeds `nil` by
value) — I asked follow-up questions until I understood it, then approved
before any code was written. Repeated per function rather than all at once.

**Design decisions made along the way:**
- Sentinel `t->nil`: `color = RB_BLACK`, and `left`/`right`/`parent` all
  self-point to `&t->nil` (defensive choice over leaving them unset).
- Key copies use a small hand-written `dup_key()` helper (malloc + memcpy)
  instead of `strdup`, since `strdup` isn't declared under strict
  `-std=c23` without a POSIX feature-test macro.
- `rb_insert`'s two allocations (node + key copy) use the goto-cleanup
  pattern per CLAUDE.md's style rule, so a malloc failure at either step
  frees exactly what succeeded and leaves the tree completely unchanged.
- New nodes are always inserted `RB_RED` (fixup is a later session);
  `rb_validate` tonight only checks BST key ordering, not color/black-height
  invariants, since those aren't meaningful until fixup exists.
- `tests/fuzz.c` uses randomized (not sorted) insertion order deliberately —
  without fixup, sorted input degenerates into a linked-list-shaped tree,
  and the recursive `rb_destroy`/`rb_validate` could overflow the stack at
  depth ~N. Random order keeps expected depth ~O(log N).

**Environment issues hit and resolved:**
- This machine's default `gcc` (11.5.0) doesn't accept the literal
  `-std=c23` flag, only `-std=c2x`; `gcc/15.2.0`+ does. Documented in
  CLAUDE.md so Claude loads that module on every build command going
  forward, since module loads don't persist between separate shell
  invocations in this environment.
- Makefile recipe lines were missing tab indentation (`make` requires a
  literal tab, not spaces) — fixed directly in the Makefile.
- `memcheck` doesn't depend on `clean` (unlike `asan`, which does), so
  running it right after `make asan` reuses the leftover ASan-instrumented
  binary and Valgrind fails confusingly. Worked around manually with
  `make clean` first; left the Makefile as-is by choice.

**Verification:** `make test`, `make asan`, and `make memcheck` all pass
clean — Valgrind reports 0 errors and all heap blocks freed on both the
unit tests and the fuzz driver.
