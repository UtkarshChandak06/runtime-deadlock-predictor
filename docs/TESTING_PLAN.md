> Historical planning document. Current implementation and verified results are in ARCHITECTURE.md and PHASE4_5_VERIFICATION.md; planned features below are not claims of support.

# Testing Plan

**Project:** Runtime Deadlock Prediction Using Lock-Order Graph Analysis
**Phase:** 0 — Planning (matrix updated in Phases 1–3)

Four layers: unit tests for the analysis modules, end-to-end tests under
`LD_PRELOAD`, an overhead/scalability benchmark, and a comparison against an
existing tool (Helgrind). Every test carries an explicit **expectation**, so
`scripts/run_tests.sh` can judge PASS/FAIL mechanically instead of a human
eyeballing log output.

---

## 1. Test Layers

| Layer | What it proves | How it runs |
|---|---|---|
| Unit (`tests/unit/`) | Graph and cycle modules are correct in isolation | Plain binaries linking `graph.c`/`cycle.c` directly — **no** LD_PRELOAD |
| Integration (`tests/integration/`) | Wrappers + tracker + graph + cycle + report work together on real programs | Each test binary run as `LD_PRELOAD=./build/libdeadlock_monitor.so ./test` under a timeout |
| Benchmark (`bench/`) | Overhead and scalability of the monitor | `scripts/run_bench.sh`: baseline vs. monitored, median of ≥5 runs |
| Comparison | Positioning against an existing approach | Same tests under Helgrind (`valgrind --tool=helgrind`), warnings compared |

---

## 2. Integration Test Matrix

Every integration test declares one expected outcome:

- `none` — exits 0, **no** `POTENTIAL DEADLOCK` in output
- `warn` — exits 0, warning **present** in output
- `hang` — real deadlock: warning present **and** process must be killed by
  `timeout`; being killed is the PASS condition
- `no-warn` — exits 0, warning **absent**, used for false-negative checks

| ID | Program | Scenario | Expected | Purpose |
|----|---------|----------|----------|---------|
| t00 | `t00_smoke.c` | Two threads lock m1→m2 in the same order, repeatedly | `none` | **Phase 1 (done):** wrappers transparent — same PASS/counter with and without preload — and per-call interception diagnostics fire |
| t10 | `t10_single_mutex.c` | One thread locks/unlocks one mutex twice | `none` | **Phase 2 (done):** tracker count 1→0 each cycle; rc==0 and errno preserved |
| t11 | `t11_nested_reverse.c` | One thread nests m1→m2, releases in reverse | `none` | **Phase 2 (done):** count 1→2→1→0; no duplicates while nested |
| t12 | `t12_two_threads_indep.c` | Two threads each lock their OWN mutex repeatedly | `none` | **Phase 2 (done):** per-thread isolation — each thread sees only its own entry, main's view untouched |
| t13 | `t13_failed_ops.c` | Error-checking mutex: failed re-lock (EDEADLK), failed unlock (EPERM) | `none` | **Phase 2 (done):** failed operations never change tracker state; error codes pass through |
| t20 | `t20_multi_thread_order.c` | 4 threads × 20 rounds, 4 mutexes in one global order | `none` | **Phase 3 (done):** wrapper-level Test F — graph updates under real concurrency stay consistent, zero false positives, all threads finish |
| t21–t26 | *(planned, Phase 4)* | lock-inversion warning, actual deadlock, gate false-positive, trylock, recursive, cond_wait | `warn`/`none` | Coming phases |
| t01 | `t01_serial_order.c` | One thread locks A→B→C, unlocks in reverse | `none` | Nested locking in one thread with no second order must be silent (a lone path is not a cycle) |
| t02 | `t02_consistent_order.c` | 8 threads all acquire locks in the same global order | `none` | Many edges, no cycle → no warning; guards against spurious DFS results |
| t03 | `t03_lock_inversion.c` | T1: A then B; T2: B then A, staggered with sleeps so both orders actually occur | `warn` | Core capability: lock-order inversion detected **before** a deadlock happens |
| t04 | `t04_actual_deadlock.c` | Classic ABBA with barriers so both threads block | `hang` (with warning) | The predictor must warn **while** threads are blocked — proves edges are recorded before the real call |
| t05 | `t05_gate_false_positive.c` | Gate-lock pattern: a cycle exists in the graph but is infeasible | `warn` but no hang | False-positive measurement case; counted, not treated as a bug |
| t06 | `t06_trylock.c` | Thread tries A, fails, then locks B — acquisition order never established | `none` | Failed trylock must add no held lock and no edge |
| t07 | `t07_recursive.c` | Recursive mutex locked 3×, unlocked 3× | `none` | Depth counting prevents self-edge `A→A` false cycles |
| t08 | `t08_cond_wait.c` *(Phase 4)* | Producer/consumer with `pthread_cond_wait` | `none` | Mutex released/reacquired by cond_wait is tracked correctly |
| t09 | `t09_consistency.c` | Any test binary run **without** LD_PRELOAD | exits normally, identical program behavior | Wrappers must be transparent: same exit code, same output, same `errno` |

Each test prints a unique marker line the harness greps for:

```c
/* every test ends with a completion marker; current tests print:
 *   printf("TEST t00 PASS (counter=%ld)\n", counter);
 *   printf("TEST t10 PASS\n");  ... same for t11, t12, t13, test_tracker
 * later tests use the convention: "TEST <id> PASS ..." */
```

---

## 3. Harness Design — `scripts/run_tests.sh`

```
for each integration test:
    compile via `make tests`
    run WITHOUT preload            -> record exit code (sanity, t09)
    run WITH preload under `timeout 10s` -> capture stderr
    judge against the test's expectation:
        none   -> exit 0 and grep -q "POTENTIAL DEADLOCK" == false
        warn   -> exit 0 and grep -q "POTENTIAL DEADLOCK" == true
        hang   -> exit 124 (timeout killed it) AND warning present
    print PASS/FAIL + the captured warning
summary line: X/Y passed; exit non-zero if any failed
```

Rules:

- `timeout` bounds every run, so a monitor self-deadlock (R6) fails the
  suite instead of hanging it.
- The same harness runs unit tests first; a unit failure skips integration.
- `make test` = build + unit + integration. One command for the whole suite.

---

## 4. Unit Test Cases (no LD_PRELOAD)

**`test_tracker.c`** *(Phase 2, done)* — empty/push/pop/depth/failed-op/
snapshot-bounds/capacity-overflow-report/per-thread isolation.

**`test_graph.c`** *(Phase 3, done)* — implements Test plan cases A–F over
`rdp_graph_add_edge` + `rdp_graph_cycle_for_edge` (see PRD section 5):

1. A: `A→B` edge stored once; same + non-inverting proposals stay silent.
2. B: `A→B` then `B→A` → CYCLE_FOUND with reconstructed path
   `A→B` (path from `A` `to` the edge's source); repeated detection of the
   same closing edge returns `ALREADY_REPORTED` (warn-once).
3. C: `A→B`, `B→C`, then closing `C→A` → CYCLE_FOUND, path len 3.
4. D: 10× `A→B` → exactly one edge + two nodes (`ADDED` once,
   `DUPLICATE` thereafter).
5. E: `A→B` and `C→D` → both stored independently no cycle; `D→A` proposal
   stays clean; cleanup truly resets edges, nodes, dropped counters.
6. F: 4 threads × 200 rounds each adding their own `me→me2→me3` chain →
   every post-storm probe returns `DUPLICATE` (edges survived), exactly
   2·N edges + 3·N nodes, dropped == 0 (deterministic, no races):
   mutex-protected record+BFS keeps the graph consistent.

---

## 5. Metrics and Evaluation (Phase 5)

All numbers go into `docs/RESULTS.md`.

| Metric | Definition | Method |
|---|---|---|
| **Detection capability** | Targeted tests where a warning appears when one is expected (t03, t04, t05) | `make test` pass rate on that subset |
| **False negatives** | Expected-warning tests that stayed silent | Must be 0 — any occurrence is a bug |
| **False-positive rate** | Warnings on programs where deadlock is infeasible / never occurs (t01, t02, t07 must be 0; t05 counted) | `warnings_expected_infeasible / total_warnings` across the suite |
| **Runtime overhead** | `(T_monitored − T_baseline) / T_baseline` on `bench_lock.c` | median of ≥5 runs each; baseline = same binary without preload; also monitor-disabled run (`RDP_ENABLE=0`) to separate interposition cost from analysis cost |
| **Scalability** | Overhead at 1, 2, 4, 8 threads × 2, 4, 8 locks | `run_bench.sh` matrix; report table, not a single number |
| **Diagnostic usefulness** | Does each warning show: detecting thread, new dependency, full cycle, conflicting order? | Rubric scored against the §8.5 format from the Review 1 document |
| **Comparison (Helgrind)** | Does Helgrind flag the same tests, and at what time cost? | `valgrind --tool=helgrind ./t0X` for t03/t04/t05; record warnings + slowdown vs. ours |

Benchmark discipline (R9): fixed workload, one warm-up run, median of ≥5
timed runs, baseline and monitored reported side by side — never a lone
percentage.

---

## 6. When Each Layer Is Written

| Phase | Tests added |
|---|---|
| 1 *(done)* | `t00_smoke` — two threads, same order; transparency (`make run-bare` vs `make run`) + per-call interception diagnostics |
| 2 *(done)* | unit `test_tracker` (empty/push/pop/depth/failed-op/snapshot-bounds/capacity-overflow/isolation) + integration t10–t13; probe self-skips tracker assertions on bare runs |
| 3 | `test_graph.c`, `test_cycle.c`, t02, t03, t04 — harness + `make test` |
| 4 | t05, t06, t07, t08 |
| 5 | Benchmark suite, scalability matrix, Helgrind comparison, `RESULTS.md` |

We deliberately do **not** write all tests up front: each phase lands with
its own tests so the suite grows alongside the implementation.

