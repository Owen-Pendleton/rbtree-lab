# rbtree-lab: project rules
## Build environment
- The default `gcc` on this machine is 11.5.0, which does NOT accept the
literal flag `-std=c23` (errors with "unrecognized command-line option").
Only gcc 14+ accepts that exact spelling; gcc/15.2.0 and gcc/16.1.0 are
available as modules and do. Do not "fix" this by changing the Makefile's
`-std=c23` to `-std=c2x` — the spec requires `-std=c23` and the grading
environment presumably has a compliant compiler.
- Module loads do NOT persist between separate tool calls in this
environment — each command runs in a fresh shell. Every command that
invokes `make` or `gcc` for this project must load the module in that same
command, e.g.:
`module load gcc/15.2.0 && make test`
Never run `make`/`gcc` bare and assume a prior module load carried over.
## Commands
- Build & unit tests: ‘module load gcc/15.2.0 && make test‘
- Sanitizers: ‘module load gcc/15.2.0 && make asan‘
Valgrind: ‘module load gcc/15.2.0 && make memcheck‘
- A change is DONE only when all three pass. Always run them; show output.
## Hard constraints
- NEVER modify include/rbtree.h. It is the graded contract.
- Check every allocation. malloc can return NULL; a NULL return must
leave the tree unchanged and return the documented error code.
- NEVER weaken, skip, or delete a test to make the suite pass. If a test
looks wrong, stop and explain why instead.
## Style
- C23. -Wall -Wextra -Werror must stay clean. No VLAs.
- Error handling: goto-cleanup pattern for multi-allocation functions.
- Prefer the smallest diff that passes. Do not refactor unrelated code.
- Every non-obvious loop gets a one-line invariant comment.
## Workflow
- For any multi-file or algorithmic change: propose a plan and wait for
approval before editing.
- Commit only from a green state; message format "M<n>: <what>".
