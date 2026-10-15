# Phase 4 + Phase 5 verification - 2026-10-13

## Implementation and scope

The existing Phase 1-3 graph/tracker algorithms and tests were preserved.
Added a complete-record warning formatter, quiet default operation, and a
bounded FIFO logger with an elected writer. No monitor mutex is held during
output or original application acquisitions. Warnings include actual Linux
TID, held/requested address identities and the complete closed cycle. Normal
traces are removed; capacity diagnostics remain. Errno, pthread return codes
and successful-operation tracking semantics are preserved.

Project relocated to `runtime-deadlock-predictor` as requested. Desktop shortcut:
`C:\Users\utkar\OneDrive\Desktop\Runtime Deadlock Predictor.lnk`.
Windows denied moving some open directories, so their contents were copied and
locked remnants retained in the old generated thread directory. The readable
folder is the current implementation and all final tests ran there.

## Files created or modified

Created:
- `include/rdp/report.h`, `src/report.c`
- `tests/integration/t30_diagnostics.c`
- `tests/unit/test_log.c`
- `tests/deadlock/real_deadlock.c`
- `tests/check_diagnostics.py`, `tests/check_sanitizers.py`
- `docs/PHASE4_5_VERIFICATION.md`

Modified:
- `src/hooks.c`, `src/log.c`, `include/rdp/internal.h`
- `Makefile`, `README.md`, `docs/ARCHITECTURE.md`
- `docs/RISKS.md`, `docs/TESTING_PLAN.md` (historical-plan labels only)

`src/graph.c`, `src/tracker.c`, their headers and all existing test sources are
unchanged. Generated logs are `phase4-{run,bare,diagnostics,sanitizers}.log`;
individual captures are in `build/diagnostics/` (removed by `make clean`).

## Exact final commands

Executed under WSL Ubuntu with GCC 15.2.0, from:

```text
/mnt/c/Users/utkar/.config/freebuff-desktop/local-projects/96d47a36-73c6-48e1-9a0a-490a4d33f70e/runtime-deadlock-predictor
```

```sh
make clean && make
make run-bare > phase4-bare.log 2>&1
make run > phase4-run.log 2>&1
make check-diagnostics > phase4-diagnostics.log 2>&1
make check-sanitizers > phase4-sanitizers.log 2>&1
```

All completed with exit code 0. Final clean build produced zero compiler
warnings with `-Wall -Wextra`. An earlier intermediate build caught a missing
local errno variable; it was fixed before this clean build and full rerun.
No final test failed. No warning suppression was added.

`check-sanitizers` prints every exact expanded GCC and timeout command to its
log. For each test `graph`, `graph_limits`, `tracker`, `log`, it compiles:

```sh
gcc -D_GNU_SOURCE -Iinclude -Wall -Wextra -O1 -g -fsanitize=thread -pthread -o build/sanitize_thread_TEST tests/unit/test_TEST.c src/graph.c src/tracker.c src/log.c src/report.c
timeout 30s build/sanitize_thread_TEST
gcc -D_GNU_SOURCE -Iinclude -Wall -Wextra -O1 -g -fsanitize=address,undefined -pthread -o build/sanitize_address_undefined_TEST tests/unit/test_TEST.c src/graph.c src/tracker.c src/log.c src/report.c
timeout 30s build/sanitize_address_undefined_TEST
```

Here TEST is the four-name expansion above; `log` additionally uses
`-Wl,--wrap=write` for deterministic write fault injection. The executable output
paths in the recorded commands are absolute paths under this same build folder.

## Test results

| Test | Expected | Actual | Status |
|---|---|---|---|
| Normal single lock, t10 | Success, no warning | Passed; empty captured stderr | PASS |
| Shared single lock | 8 threads succeed, no warning | Passed; empty stderr | PASS |
| t00 consistent A -> B | Counter 10; no warning | Counter 10; empty stderr | PASS |
| t11/t12/t20 safe nesting/concurrency | Success, no cycle output | Passed; empty stderr | PASS |
| Two-lock cycle | Full A -> B -> A with correct fields | Exactly one matching five-line record | PASS |
| Three-lock cycle | Full A -> B -> C -> A | Exactly one matching five-line record | PASS |
| Warning correctness | Exact heading, TID, held/requested, full closed path | Parsed records exactly match actual address/TID expectations | PASS |
| Repeated closing dependency | Unique edge count, bounded output | 20 repeats: one report per closing edge | PASS |
| New cycle information | New closing edge reports despite prior cycle | Both two-lock and three-lock records emitted | PASS |
| Concurrent warnings | Complete independent records | Eight three-lock reports; no stray/interleaved text; 24 unique edges | PASS |
| Concurrent maximum records | Eight intact 1024-node reports under short writes | All eight byte-identical to complete reference record | PASS |
| Maximum path | 1024 nodes plus closure, no truncation | Exact path matches all 1025 printed addresses | PASS |
| t13 failed lock/unlock | EDEADLK/EPERM preserved, tracker unchanged | Passed; conservative self-cycle warning | PASS |
| t21 integration regression | Inversion, errno, attempted-edge tracking | Passed, exactly two expected warnings | PASS |
| Graph A-F | Single, 2/3 cycles, duplicate, independent, concurrent | All passed in monitored/bare targets | PASS |
| Graph boundaries | Full path, limits in both APIs, optional outputs, reset/init | All existing assertions passed | PASS |
| Concurrent cycle closure | Exactly one closure per round | 100 rounds passed | PASS |
| Tracker unit regressions | Push/pop/depth, snapshots, overflow, TLS isolation | All passed in both targets | PASS |
| Tracker wrapper capacity | 64 tracked + one loss; empty after unlocks | Accurate single incomplete warning; state assertions passed | PASS |
| Graph node/edge wrapper capacities | Rejection counted, successful acquisition still tracked | One accurate incomplete warning per fixture; assertions passed | PASS |
| Logger queue capacity | Blocked writer; 16 queued; four excess dropped | Exact dropped count and explicit incomplete summary; no producer wait on I/O | PASS |
| EINTR/short writes | Retry correctly, complete output | Exact bytes matched | PASS |
| EIO/zero write | Return without looping; count error; preserve errno | Both counted; recovered next record | PASS |
| /dev/full | ENOSPC cannot corrupt graph/tracker | Error counter nonzero; all state/errno assertions passed | PASS |
| Broken pipe | No SIGPIPE termination; same application results | Exit zero, error counted, state intact | PASS |
| Deliberate real deadlock | Report before strict timeout; runner survives | One complete warning, child killed/reaped at 2s | PASS |
| False-positive demonstration | Sequential inversions warn but complete | Two/three-lock fixtures returned zero | PASS |
| Bare regression target | Same application success; probes skip unsupported monitor assertions | Every test passed | PASS |
| Bare diagnostics variants | No monitor output | two/three/concurrent/long/new: empty stderr | PASS |
| ThreadSanitizer | No detectable races | graph, graph_limits, tracker, log all passed | PASS |
| AddressSanitizer/UBSan | No memory/undefined-behavior findings | Same four suites all passed | PASS |
| Trylock/timedlock/condition/lifecycle | No unsupported coverage claim | No wrappers added, no claimed monitoring tests | UNSUPPORTED |

## Actual captured warnings

The following records are copied from the final diagnostics captures, not
illustrative invented addresses. They came from separate subprocesses.

Two-lock cycle:

```text
[WARNING] Potential Deadlock Detected (not a confirmed deadlock)
Thread: tid=1275
Held Lock: 0x61eb8cf600a8
Requested Lock: 0x61eb8cf60080
Cycle: 0x61eb8cf60080 -> 0x61eb8cf600a8 -> 0x61eb8cf60080
```

Three-lock cycle:

```text
[WARNING] Potential Deadlock Detected (not a confirmed deadlock)
Thread: tid=1277
Held Lock: 0x573fdad690d0
Requested Lock: 0x573fdad69080
Cycle: 0x573fdad69080 -> 0x573fdad690a8 -> 0x573fdad690d0 -> 0x573fdad69080
```

The harness independently prints expected fields using the actual mutex
addresses and executing TID, compares every parsed warning, and rejects leftover
stderr text. Safe fixtures t00, t10, t11, t12, t20 and shared-single-lock have
zero-byte captured stderr files. Bare cycle variants also have empty stderr.
This establishes quiet behavior and complete warning correctness automatically.

## Interpretation, remaining limits, and unexecuted coverage

All requested test categories were exercised except unsupported synchronization
operations, intentionally not added. No available final test was skipped or
failed. Sanitizers cover standalone modules, not interposed integration runtime.
The canceled-writer cleanup path is implemented but not directly fault-injected;
asynchronous cancellation and signal-handler monitoring are unsupported.

A warning predicts risk from historical acquisition order, not inevitability.
The two/three sequential fixtures run on one application thread: their constraints
prevent a deadlock in that run, yet the historical graph has a cycle and warns.
The intentional deadlock fixture separately verifies pre-blocking detection.

Bounds remain 64 held locks per thread, 1024 graph nodes, 4096 graph edges, and
16 queued log records. Full paths are supported up to the graph bound. Capacity
loss, a failed sink, process termination or cancellation can lose diagnostics;
errors/losses are counted and queue loss is explicitly reported when output can
progress. Reporting deduplication tracks attempts, not successful delivery.
A blocked sink can stall the elected reporting thread. No internal monitor mutex
is held then, and other producers can enqueue/drop safely. Application stderr
or other-process writes are not serialized by this library.

Opaque addresses can alias new mutexes after destroy/reinit. Self-attempts on
recursive/error-checking mutexes can produce conservative warnings. Condition
waits can leave tracker state stale. Robust owner death, trylock/timedlock,
lifecycle interception, static executables, cross-process synchronization,
fork with active monitor state and unloading an active monitor remain unsupported.
These limitations can cause false positives or missed deadlocks; the project
does not claim universal detection. No performance figures were produced.

Combined Phases 4/5 complete. Phase 6 benchmarking and GUI work not started.
