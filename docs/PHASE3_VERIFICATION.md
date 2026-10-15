# Phase 3 verification - 2026-10-13

Repository: nested thread directory
`threads/e97fa52738cb3c8acc42672193d33d7758c304d7a75fc5aebe14dca915a8e618`.
Executed using Ubuntu under WSL, from that directory. Existing graph code was
present when inspected and was corrected, rather than assumed absent.

## Commands and results

```sh
make clean && make
make run > phase3-run.log 2>&1
make run-bare > phase3-bare.log 2>&1
```

All exited successfully. GCC used `-Wall -Wextra`; zero warnings. The Makefile
runs every test with `timeout 15s`; none timed out. Tests execute under preload
in `make run` (unit tests directly link modules), and without preload in
`make run-bare`. Every listed test passed in both targets:

| Test | Actual result |
|---|---|
| Graph A: single dependency | Edge stored, no cycle |
| Graph B: two locks | A -> B -> A detected; repeated closing edge suppressed |
| Graph C: three locks | A -> B -> C -> A detected |
| Graph D: duplicates | One stored edge |
| Graph E: independent dependencies | No cycle |
| Graph F: concurrent updates | Correct counts and preserved edges |
| Graph limits: full path | All 1024 tokens returned in order |
| Graph limits: initialization | Repeated init preserved graph history |
| Graph limits: node capacity | Both APIs rejected excess; dropped counter incremented |
| Graph limits: edge capacity | Both APIs rejected excess without adding nodes |
| Graph limits: optional outputs | NULL length/output pointers accepted |
| Graph limits: invalid/self tokens | Invalid token incomplete; self-cycle reconstructed |
| Graph limits: concurrent cycle | Exactly one closure per round, 100 rounds |
| Tracker unit | Empty state, push/pop, recursion, failed ops, snapshot bounds, overflow, TLS isolation passed |
| t00_smoke | PASS, counter=10 |
| t10_single_mutex | PASS |
| t11_nested_reverse | PASS |
| t12_two_threads_indep | PASS |
| t13_failed_ops | PASS; EDEADLK/EPERM behavior preserved |
| t20_multi_thread_order | PASS; no cycle warning in monitored log |
| t21_sequential_cycle | PASS; graph probes, inversion, errno, failed-attempt recording |

Bare tracker integration probes intentionally skip monitor-only assertions.
The monitored t21 log contains exactly two warnings: inversion and self-cycle.
t13 also warns about its error-checking self-attempt; this is documented
conservative graph behavior, not a confirmed deadlock. No FATAL resolver message
was present in the monitored log.

Additional exact commands, all successful with no sanitizer diagnostics:

```sh
gcc -D_GNU_SOURCE -Iinclude -Wall -Wextra -O1 -g -fsanitize=thread -pthread -o build/graph_tsan tests/unit/test_graph_limits.c src/graph.c
timeout 30s build/graph_tsan > phase3-tsan.log 2>&1
gcc -D_GNU_SOURCE -Iinclude -Wall -Wextra -O1 -g -fsanitize=address,undefined -pthread -o build/graph_asan tests/unit/test_graph_limits.c src/graph.c
timeout 30s build/graph_asan
gcc -D_GNU_SOURCE -Iinclude -Wall -Wextra -O1 -g -fsanitize=thread -pthread -o build/graph_af_tsan tests/unit/test_graph.c src/graph.c
timeout 30s build/graph_af_tsan
```

ThreadSanitizer covers the standalone graph tests, not the preload integration.
Passing tests do not constitute proof of absence of all races or deadlocks.

## Files changed during this work

Modified: `include/rdp/graph.h`, `src/graph.c`, `src/hooks.c`,
`include/rdp/internal.h`, `Makefile`, `README.md`, `docs/ARCHITECTURE.md`.
Added: `tests/unit/test_graph_limits.c`,
`tests/integration/t21_sequential_cycle.c`, `docs/PHASE3_VERIFICATION.md`.
Existing tracker code and existing test sources were preserved. Build artifacts
and phase3 logs are generated verification output.

## Limits and completion

All requested Phase 3 work completed. Graph storage is static, so there is no
runtime graph allocator to fault-inject; both capacity exhaustion paths are
tested instead. Limits: 1024 nodes, 4096 edges, 64 held locks per thread;
address reuse, mutex types, trylock/timedlock, condition variables and lifecycle
interception remain unsupported. Recursive/error-checking self-attempts can
warn. Warnings describe potential deadlocks only. See ARCHITECTURE.md for
additional runtime limitations. No benchmarking, GUI or Phase 4 work performed.
