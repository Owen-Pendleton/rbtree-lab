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


Changes I made when implementing the deletion sections:

A missing required test case. The notes add a 5th case to Milestone 2's list: a black node with a single red child, tested specifically to confirm the code doesn't enter the sibling-case loop — x is red, the loop condition (x != root && x->color == BLACK) is false immediately, and the fixup's tail (x->color = BLACK) settles it in one line. My "root deletion" test (50,30, delete "50") technically exercises this, but only as a root, which the spec treats as a separate case — they want it shown as its own scenario, presumably including a non-root instance.

Good news: I already have a verified non-root instance sitting in the tree from test case 2. Recall the shape from 61,06,40,91,79,76:
40(B)[ 06(B) leaf, 79(R)[ 61(B)[nil, 76(R) leaf], 91(B) leaf ] ]
61 is black with exactly one child, 76, which is red — non-root, and not the node case 2 deletes. Deleting "61" from that same insert sequence gives x = 76 (red), removed_color = BLACK, and fixup's loop must not fire. That's a clean 5th table row reusing an insert sequence we've already validated, no new simulation needed.

One bug I introduced and fixed along the way, worth flagging since it's the kind of thing that bites people in this class too: I initially validated the whole tree (rb_validate, an O(n) walk) after every single operation, which turned the fuzz loop into O(n²). It passed fine at n=100000 (just slow enough not to notice) but hung past n=200000. Fixed by checking rb_size (O(1)) every iteration, and rb_validate at ~200 checkpoints spread across the run plus once at the end — enough to still localize a broken invariant to roughly the right region, without losing linear scaling. Reran at n=1, 2, 5, and 500000 to confirm — 500000 now finishes in ~2s..
