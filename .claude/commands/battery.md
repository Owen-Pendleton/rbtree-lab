Run make test, make asan, and make memcheck in sequence.
Run make clean between make asan and make memcheck, because Valgrind cannot run the sanitizer-built binaries.
If any fail, stop and diagnose the first failure: $ARGUMENTS
Show all output. Do not modify tests to make them pass.
