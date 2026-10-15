# Runtime Deadlock Prediction Using Lock-Order Graph Analysis

Linux LD_PRELOAD monitoring in C, through combined Phases 6 and 7.
The library tracks attempted mutex acquisition orders and reports complete
**potential deadlock** cycles. A cycle is not proof that a deadlock occurs.

## Build and verify

Use Linux or WSL Ubuntu with GCC, GNU Make, Python 3, and GNU coreutils:

```sh
make clean
make
make run-bare
make run
make check-diagnostics
make check-sanitizers
make check-robustness
make benchmark
```

GCC uses `-Wall -Wextra`. The regression targets run each test with a 15-second
timeout. Diagnostics tests capture subprocess output and assert warning fields,
full paths, quiet safe executions, and warning counts. The deliberate deadlock
fixture is run ONLY by that harness, with a two-second timeout and kill/reap;
do not run `build/real_deadlock` directly. Sanitizer executables have 30-second
timeouts. Resolver fault injection is covered by `make check-robustness`.

`make run` preloads the monitor into integration tests. `make run-bare` runs the
same programs without the monitor; graph/tracker probes then skip monitor-only
assertions. Unit tests link modules directly in both targets.

## Performance benchmark

`make benchmark` compiles a separate mutex workload and compares the same
binary without and with `LD_PRELOAD`. It covers single-thread, independent,
shared, and consistently ordered locking at 1, 2, 4, and 8 threads where
applicable. The runner calibrates the work, alternates mode order, takes five
samples per mode, validates operation counts and monitor presence, and uses the
median of `CLOCK_MONOTONIC` elapsed times. Override runner options with, for
example, `make benchmark BENCH_ARGS="--runs 7 --target-seconds .2"`.

Each run creates `benchmarks/results/<timestamp>/` containing `raw.csv`,
`calibration.csv`, `summary.csv`, `environment.json`, and `median.svg`; existing
results are never overwritten. The corrected, audited 2026-10-15 WSL2 run is
in `benchmarks/results/20261015T224443+0530/`. Its measured overhead ranged from
137.30% (shared, 2 threads) to 1513.68% (ordered, 1 thread). Startup and monitor
initialization are outside the timed interval. These measurements apply only to
this machine, build, and synthetic workload; they are not universal performance
characteristics.

## Presentation frontend

An interactive project presentation is available in `frontend/`. From the
project root, serve it with:

```sh
python3 -m http.server 8080 --directory frontend
```

Then open `http://localhost:8080`. The browser demonstration explains the
algorithm and visualizes audited data; it does not execute `LD_PRELOAD` or C
programs in the browser.

## Monitor a program

```sh
LD_PRELOAD="$PWD/build/libdeadlock_monitor.so" ./your_program
```

Normal locking is quiet by default. A cycle produces a record containing:

```text
[WARNING] Potential Deadlock Detected (not a confirmed deadlock)
Thread: tid=<Linux thread ID>
Held Lock: <address H>
Requested Lock: <address L>
Cycle: <L> -> ... -> <H> -> <L>
```

The addresses are opaque tokens, never dereferenced by the graph. Thread IDs
are actual Linux kernel TIDs, not invented thread labels or source locations.
The complete record, including the closing edge, is emitted without mixing
with other monitor records. Application-written stderr output is independent.

`hooks.c` snapshots held locks and analyzes each H -> L **before** the original
potentially blocking lock call. The graph mutex is released before reporting.
Successful lock/unlock return codes alone update the existing TLS tracker.
`graph.c` uses a synchronized adjacency matrix and BFS from L to H; matrix BFS
costs O(V^2) time and O(V) scratch. `report.c` formats the complete path, and
`log.c` queues bounded records with one elected writer. No internal mutex is
held during output or application acquisition.

## Bounds and warning semantics

- Graph: 1024 nodes and 4096 unique directed edges, static storage ~1.008 MiB.
- Tracker: 64 distinct held locks per thread, with recursion depths.
- Cycle: all 1024 nodes plus the repeated closing node can be printed.
- Logger: 16 queued records, each up to 25,112 bytes on 64-bit Linux
  (~393 KiB total queue storage); no heap allocation in record submission.
- Deduplication: at most one report attempt per directed closing edge. New
  cycle-closing edges still report. The monitor does not enumerate all cycles.

Graph/tracker overflow reports incomplete monitoring. Queue overflow is counted
and produces an explicit incomplete-diagnostics record when output progresses.
Write failures are counted (`rdp_log_errors`); queue losses are available via
`rdp_log_dropped`. A failed output sink cannot be relied on to display its own
failure message. Logging errors preserve application errno and monitor state.

## Limitations

Historical orders can be infeasible because of gates, sequential execution or
failed attempts. Our sequential inversion tests deliberately warn and finish:
a false-positive demonstration, not a confirmed hang. Recursive/error-checking
self-attempts can warn because mutex types are unknown. Deduplication records
report attempts, not successful delivery; a dropped/failed warning is not retried
for that same closing edge. Stalled stderr can block the elected writer, but
other reporting threads can enqueue or record overflow without waiting for I/O.

Mutex addresses can be reused after destruction; lifecycle interception is not
implemented. Trylock, timedlock, condition waits, robust owner-death recovery,
static executables and cross-process synchronization remain unsupported.
Fork with active monitor state, signal-handler use, asynchronous cancellation,
and dynamically unloading an active monitor are unsupported. See the architecture
for logging cancellation and process-exit limitations.

See [architecture](docs/ARCHITECTURE.md), the historical
[Phase 3 report](docs/PHASE3_VERIFICATION.md), and the current
[Phase 4/5 verification report](docs/PHASE4_5_VERIFICATION.md). Phase 6/7
results, commands, robustness classifications, and remaining risks are in
[the Phase 6/7 verification report](docs/PHASE6_7_VERIFICATION.md).
