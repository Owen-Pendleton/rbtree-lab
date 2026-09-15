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

## 2026-09-14 — Evening 3

**Scope:** double-black case table for `delete_fixup` (one isolated unit
test per branch, mirrors included), plus fuzzer hardening: fixed seed,
configurable validate cadence, and an automatic shrink-on-failure pass.

**Finding coverage was thinner than it looked:** traced all 5 existing
`delete_cases[]` rows by hand before adding anything. Only "black leaf, red
sibling" actually drove `delete_fixup`'s sibling-rebalancing loop at all
(and it chains sibling-red straight into far-red-nephew, rather than
isolating one branch) — the other 4 rows exercise red-leaf removal,
two-children swap, and the two "loop never runs" terminal cases, none of
which touch the sibling-based cases. Rather than hand-derive 7 more tree
shapes by arithmetic (error-prone), built a throwaway instrumented copy of
`rbtree.c` in scratch (one case-id tag per branch) and brute-force searched
small insert/delete sequences until each of the 8 branches (A1-A4, and
their B1-B4 mirrors for x being the right child) had a minimal example,
replayed deterministically and dumped for review before writing anything.
Confirmed by construction that A1/A3 (and mirrors B1/B3) can never fire
alone — the code always falls through into a terminal case (2 or 4; 6 or 8)
within the same `delete_fixup` call — so those four rows are documented as
minimal 2-branch chains rather than forced isolations. `delete_cases[]` now
has 12 rows; `make test` green.

**Fuzzer changes (`tests/fuzz.c`):** it never called `srand()` at all
before tonight (silently defaulting to glibc's implicit seed) — added
`argv[2]` as an optional seed, default `1337`, documented here and in the
success/failure output so a run is citable. Added `argv[3]` as an optional
`rb_validate` cadence override (default stays today's `n/200` so `make
test`/`asan`/`memcheck` are unaffected). Biggest addition: on any mismatch,
the fuzzer now logs every operation's decision as it's drawn from `rand()`
and, on failure, runs a delta-debugging (ddmin) pass that replays reduced
candidate sequences against a fresh tree/model until no operation can be
dropped without the failure disappearing — so a failure prints an
already-minimized repro instead of a multi-thousand-line log.

**Verification:** `make test`, `make asan`, `make memcheck` all green.
Additionally ran the fuzzer directly (not through the Makefile, since the
params are non-default) at 100,000 ops / seed 1337 / validate-every-100
under both an ASan build and Valgrind — both clean, no bug found at this
seed. Since a clean run means the shrink path never actually fires, tested
it separately: patched a throwaway scratch copy of `rbtree.c` to skip one
recolor in case A4, ran the real (unmodified) fuzzer against it, and
confirmed it caught the broken invariant at op 2401/100000 and shrank the
repro down to 11 operations. `src/rbtree.c` itself was not touched this
session.

## 2026-09-14 — Adversarial review

**Scope:** an adversarial review hunting three named bug families in
`src/rbtree.c`: a use-after-free in the successor splice during deletion, a
key copy leaked on the overwrite-existing-key path of insertion, and an
allocation whose NULL return nobody checks. Traced each by hand first, then
verified the two non-obvious conclusions empirically with throwaway mutated
copies in scratch (same methodology as Evening 3's fuzzer-hardening test) —
never touching `src/rbtree.c`, `tests/`, or `include/rbtree.h`.

**False positive — UAF in the successor splice (`rb_delete`,
`rbtree.c:288-338`):** this shape is a classic UAF trap in the textbook
pointer-relinking delete (splice the successor node into `z`'s tree
position, then read stale fields off the old `z`/`y` afterward), so it was
the first thing I checked. But this implementation doesn't relink pointers —
it swaps `key`/`value` *by value* between `z` and its successor `y`
(`:310-316`), then reassigns `z = y` (`:318`) so the node that actually gets
unlinked and freed is the one already destined for it. Tracing reads vs.
frees: `transplant` at `:324` only rewires parent/child pointers, never
calls `free`; `z->color` is read at `:325`, right after `transplant` but
before any free; `delete_fixup` (`:327-328`) operates on `x` and its
ancestors only, never touches `z`. The actual frees
(`t->value_free(z->value)`, `free(z->key)`, `free(z)`) don't happen until
`:331-335`, after every read of `z` completes. To confirm the shape really
is dangerous in general (not just theoretically), I mutated a scratch copy
to move the frees up before the `z->color` read and ran it under ASan
against the real `test_rbtree.c`: it reliably aborts with
`AddressSanitizer: heap-use-after-free ... READ of size 4` at the mutated
`z->color` line, with the free and the original allocation both captured in
the report. So the pattern is a legitimate thing to check — this code just
avoids it by construction.

**Real finding — coverage gap on the overwrite path (`rb_insert`,
`rbtree.c:154-160`):** on `cmp == 0`, the code frees the old *value* via
`value_free` (`:156-157`) and stores the new value (`:159`), but never
touches `x->key` and never calls `dup_key()` here — since the key already
matched, reusing the existing copy is correct, so nothing is allocated and
nothing can leak on this path today. The gap is in verification, not
behavior: `tests/test_rbtree.c:114-118` is the only test exercising this
branch, and it asserts solely on `free_count` (the value's free-counter,
via `counting_free`) — nothing in the suite asserts anything about the key
copy. I proved this empirically: mutated a scratch copy to add a spurious
`dup_key()` refresh inside the `cmp == 0` branch that overwrites `x->key`
without freeing the old one, then ran the real, unmodified
`tests/test_rbtree.c` against it — `all tests passed`, exit code 0, every
assertion green, leak and all. Only `valgrind --leak-check=full` against
that same mutant caught it (`6 bytes in 1 blocks are definitely lost ...
by dup_key ... by rb_insert`). So the family-2 contract clause
(`rbtree.h:11`) is correctly implemented, but it has exactly one layer of
defense — Valgrind — rather than two; a dedicated assertion on key-copy
identity/count in the overwrite test would close the gap without changing
any production code.

**Family 3 — unchecked allocation:** no finding. Every `malloc` site in
`src/rbtree.c` (`rb_create` at `:27`, `dup_key` at `:49`, `rb_insert`'s
`node` at `:169` and `key_copy` at `:173`) is checked on the very next
line, and `rb_insert`'s `goto cleanup` (`:197-201`) frees exactly what
succeeded and returns `-1` without mutating the tree, matching
`rbtree.h:12-13`'s contract exactly.

**Verification:** `make test`, `make asan`, `make memcheck` all green on the
untouched tree (0 leaks, 0 sanitizer errors, 33,194+270 allocs all freed).
Both mutation experiments ran only against scratch copies of `rbtree.c` and
the real, unmodified `tests/test_rbtree.c` — nothing under `src/`, `tests/`,
or `include/` was changed by this session. `git status`/`git diff` after
this entry shows only `PROMPTLOG.md` touched.
