# Phase 1 verification — 2026-10-25

## Inspected environment

| Tool/environment | Observed version/status |
| --- | --- |
| Host shell | Native Windows PowerShell |
| Windows OS version | 10.0.26300.0 |
| WSL distribution | Ubuntu, running, WSL version 2 |
| Ubuntu | 26.04.1 LTS |
| Linux kernel | 6.18.33.2-microsoft-standard-WSL2 |
| Linux GCC | 15.2.0 |
| Linux GNU Make | 4.4.1 |
| Linux Git | 2.53.0 |
| Linux Python | 3.14.4 |
| Linux-native Node/npm | No Node executable found; npm resolves to the Windows installation |
| Windows GCC | MinGW 6.3.0; not used for this project |
| Windows Git | 2.56.0.windows.2 |
| Windows Node / npm | 24.19.0 / 11.17.0 |
| Windows Make / Python | Not found on the inspected PowerShell PATH |
| VS Code | 1.143.1, x64 |
| VS Code WSL extension | Absent from installed extension list; manual installation needed |

The workspace initially contained no project implementation or Git repository.
The new project was created in `deadlock-sentinel/` without reusing a monitor.

## Actual Linux execution

Executed in Ubuntu/WSL2 from the new project's directory, including the space in
the parent path:

```text
$ make clean
rm -rf build
$ make
mkdir -p build
gcc -std=c11 -Wall -Wextra -Wpedantic -g -pthread examples/basic/hello_threads.c -o build/hello_threads
$ make run
./build/hello_threads
Deadlock Sentinel - Phase 1
Worker thread 1 started
Worker thread 2 started
Both threads completed successfully
$ bash scripts/check_phase1.sh
make: Nothing to be done for 'all'.
Deadlock Sentinel - Phase 1
Worker thread 1 started
Worker thread 2 started
Both threads completed successfully
PASS: both workers printed once and the program exited successfully.
```

All four commands succeeded; compilation produced no warnings. `make help` also
succeeded and described all four targets. Git initialization succeeded locally,
and `git check-ignore build/hello_threads` confirmed the executable is ignored.
No system packages were installed and no remotes or publications were created.

These are basic successful-path checks, not proof of scheduler ordering or
exhaustive error handling. Thread-creation/join failures were not fault-injected.
VS Code's interactive WSL connection remains unverified. No Phase 2–7 feature
or runtime-overhead result is claimed.

## Phase 2 verification — 2026-10-25

The Phase 1 section above is a historical record. Phase 2 is now implemented and
verified in the same Ubuntu/WSL2 workspace; Phases 3–7 remain unimplemented.

Executed successfully:

```bash
make clean
make
make run
make banking-safe
make banking-unsafe
make check-phase2
bash scripts/check_phase1.sh
python3 tests/test_banking.py --deadlock-only
make help
```

All C files compiled with GCC, C11, `-Wall -Wextra -Wpedantic`, and `-pthread`,
without warnings. The original Phase 1 C example and check script were unchanged.

Safe and unsafe runs both reported actual computed values:

```text
INITIAL account1=100000 account2=100000 total=200000
FINAL account1=105000 account2=95000 total=200000
VALIDATION PASS
```

The unsafe run's successful acquisitions were worker 1: account 1 then 2;
worker 2: account 2 then 1. Worker 1 released both locks and was joined before
worker 2 started. Both transactions completed without an actual deadlock.

Final full-suite results:

```text
PASS: 20 safe + 20 unsafe runs; balances, totals, nested orders, completion
PASS: invalid inputs, insufficient funds, overflow, exact limits, lock release
PASS: invalid CLI arguments and direct-launch guard
PASS: SIGINT parent interruption leaves no fixture
PASS: SIGTERM parent interruption leaves no fixture
PASS: Phase 2 controlled workload checks
PASS: both workers printed once and the program exited successfully.
```

Three controlled-deadlock trials in the suite each reached the expected state,
timed out at 2.5 seconds, were killed and reaped, and left no process group. A
separate `--deadlock-only` run also passed. One actual evidence sequence was:

```text
HELD worker=1 account=1
HELD worker=2 account=2
REQUEST worker=2 held=2 wanted=1
REQUEST worker=1 held=1 wanted=2
WAIT_EVIDENCE tasks=3 wchan=futex_do_wait,futex_do_wait,futex_do_wait
Controlled test: expected deadlock reached
Child process terminated by test harness; reaped; process group absent
```

An initial interruption test exposed a subprocess-startup race before the parent
had stored the child's PID. The harness now defers SIGINT/SIGTERM through that
creation window, then restores signal delivery inside its cleanup scope. Both
interruption checks and the full suite passed after the fix. A final process-list
check found no banking processes.

Limits: the evidence establishes the known controlled fixture's circular wait,
not general-purpose deadlock detection. `/proc` task wait-state visibility is
required. Tests do not fault-inject pthread API failures or measure performance.
The two-account workload has fixed amounts; it is not a configurable banking
application. SIGKILL of the parent and host crashes are outside its cleanup
guarantees. VS Code GUI checks from Phase 1 remain unverified.

## Phase 3 verification — 2026-10-25

Phases 1–3 are now complete; the earlier sections record their original runs.
Phases 4–7 remain unimplemented. Phase 3 events are supplied through the C API,
not intercepted from the banking programs.

A fresh clean build and the following full sequence passed in Ubuntu/WSL2:

```bash
make clean
make
make run
make banking-safe
make banking-unsafe
make check-phase2
make analysis-demo
make check-phase3
bash scripts/check_phase1.sh
make check-phase3-sanitize
make check-phase3-tsan
```

The full post-clean command output is saved in `build/phase3-verification.txt`
(generated and ignored; `make clean` removes it). Compilation produced no warnings.
The static archive contains graph, tracker, and analyzer objects; no preload shared
library was created. Phase 1 and banking source/check files were preserved.
Before/after SHA-256 hashes matched for all existing basic/banking source files,
banking tests, and Phase 1/2 check scripts. Final process inspection found no
banking, analyzer-test, or graph-demo processes left running. `make help`, Bash
syntax validation, and static-archive member inspection also passed.

| Check | Actual outcome |
| --- | --- |
| Phase 1 build/run/check | Passed |
| Safe and unsafe banking | Passed; final balances 105000 and 95000 cents, total 200000 |
| Phase 2 suite | 20 safe + 20 unsafe runs; boundary/CLI checks; SIGINT/SIGTERM cleanup checks passed |
| Controlled deadlock regression | Three bounded harness trials passed with held/request/futex evidence; children reaped |
| Graph suite | Passed: empty/single/duplicate edges, 2/3/32-node cycles, shortest BFS witness, DAG, disconnected components, self-edge, reset, invalid IDs, node/edge/path limits |
| Tracker suite | Passed: pending vs acquired, failure semantics, nested entries, recursive depth, unknown/failed release, thread separation, held/depth exhaustion |
| Analyzer suite | Passed: banking event orders, historical failed requests, self-lock policies, recursive reentry, multi-warning requests, partial-batch incompleteness |
| Concurrent suite | 20 runs x 8 workers x 100 event sequences = 16000 sequences, with concurrent snapshot queries; exactly one closing-edge warning per run |
| AddressSanitizer + UndefinedBehaviorSanitizer | All four Phase 3 test binaries executed successfully, no reported errors |
| ThreadSanitizer | Concurrent test executed successfully, no reported races |

Actual demo output:

```text
Phase 3: supplied events drive the real analysis API; no live interception.
A warning means potential risk, not proof of a currently blocked application.
safe_order warnings=0
type=potential_lock_order_cycle thread=303 held_lock=20 requested_lock=10 closing_edge=20->10 sequence=2 cycle=10->20->10
opposite_order warnings=1
type=potential_lock_order_cycle thread=606 held_lock=30 requested_lock=10 closing_edge=30->10 sequence=3 cycle=10->20->30->10
three_lock_order warnings=1
```

Limits remain explicit: bounded storage, caller-supplied identities/kinds/events,
historical-request false positives, no automatic runtime instrumentation or
recovery, and no claim that a cycle establishes a current deadlock. Sanitizer
success applies to these executed tests, not all possible schedules. Internal
pthread failures and sequence-counter overflow were not fault-injected. See
GRAPH_ANALYSIS.md for the exact API and lifecycle/completeness contracts.

## Phase 4 verification — 2026-10-26

Phases 1–4 are complete. Earlier sections describe earlier milestones, not the
current implementation boundary. Phases 5–7 remain unimplemented.

A clean Ubuntu/WSL build executed the full requested sequence successfully:

```bash
make clean
make
make run
make banking-safe
make banking-unsafe
make check-phase2
make analysis-demo
make check-phase3
make monitor-build
make monitor-safe
make monitor-unsafe
make check-phase4
bash scripts/check_phase1.sh
make monitor-deadlock
make check-phase4-ubsan
make check-phase3-sanitize
make check-phase3-tsan
```

The complete post-clean console log is `build/phase4-verification.txt`. Generated
logs/artifacts are ignored and removed by `make clean`. The real runtime library
was compiled with C11, warning flags, PIC, pthread support, shared linking, and
libdl. There were no compiler warnings or sanitizer diagnostics in this run.

Before the clean build, before/after SHA-256 checks confirmed the existing banking
sources/tests and Phase 3 engine sources/headers were unchanged. The runtime uses
new PIC builds of the same engine, without changing its public API.

| Check | Actual result |
| --- | --- |
| Phase 1 | Build, run, regression script passed |
| Phase 2 | 20 safe + 20 unsafe runs, validation/CLI checks, three bounded deadlock trials, interruption cleanup passed |
| Phase 3 | Unit suites and 20 concurrent runs passed |
| Runtime safe banking | Five preloaded runs passed; four request/acquisition/release records each; zero cycle warnings; final total unchanged |
| Runtime unsafe banking | Five preloaded runs passed; one full two-account cycle per run; balances and normal exit matched baseline |
| Internal-lock exclusion | Runtime request identities exactly matched the two account initialization identities |
| Thread identity/lifetime | Actual fixture TIDs matched event TIDs; six concurrent and six sequential tracker lifetimes passed; JSON records parsed without interleaving |
| Mutex semantics | Explicit recursive depth 1/2/1/0; EDEADLK/EPERM and errno preservation; failed operations did not mutate held state |
| Unknown/static types | Ordinary static mutex tracked with unknown-kind notice; static recursive case warned conservatively and marked incomplete |
| Lifecycle/limits | Reinitializing one address produced three distinct generations; 33 graph nodes and 65 registry entries exposed capacity failure |
| Unsupported operations | Robust initialization marked incomplete; EOWNERDEAD preserved as lock_owner_dead, not ordinary failure; trylock ownership gap exposed at successful unlock |
| Output/configuration | Dedicated file FD, pipe, stderr fallback, disabled mode, invalid settings, closed/read-only FD, broken pipe, full nonblocking pipe all passed |
| Monitored actual deadlock | Two trials in normal suite plus separate dedicated run: opposite requests/held locks, full warning before 2.5-second timeout, three futex waits; child killed/reaped, group absent |
| Parent interruption | SIGINT and SIGTERM runtime harness tests left no child or process group |
| Deferred cancellation | Pending deferred cancellation preserved until the fixture's own cancellation point; later application locking passed |
| Runtime UBSan | Full integration suite passed with libdeadlock_monitor_ubsan.so; no UB diagnostics |
| Analysis ASan/UBSan | All four analysis suites executed successfully |
| Analysis TSan | Concurrent analysis test executed successfully |

ASan/TSan were tested on standalone Phase 3 analysis binaries, not combined with
the preload runtime. No runtime ASan/TSan compatibility claim is made. Runtime
UBSan is a distinct tested target.

Actual safe banking final output:

```text
FINAL account1=105000 account2=95000 total=200000
VALIDATION PASS
```

Actual unsafe runtime warning from `build/runtime-tests/unsafe.jsonl`:

```json
{"schema_version":1,"session_event":"potential_lock_order_cycle","timestamp_ns":16468482635930,"process_id":4203,"thread_id":4205,"thread_instance":2,"sequence":16,"monitoring_complete":true,"held_lock":"lock_2","requested_lock":"lock_1","closing_edge":["lock_2","lock_1"],"graph_sequence":2,"cycle":["lock_1","lock_2","lock_1"]}
```

Actual controlled fixture stdout from the separate monitored harness run:

```text
HELD worker=1 account=1
HELD worker=2 account=2
REQUEST worker=2 held=2 wanted=1
REQUEST worker=1 held=1 wanted=2
```

The corresponding cycle record has PID 4292, TID 4293, event sequence 12, and
cycle `lock_2 -> lock_1 -> lock_2`. Both circular request pairs were observed before
the parent timed out; Linux task wait channels showed three futex waits (two
workers plus joining main). Timeout alone was not accepted as evidence.

The fixture emitted no final balance/completion record; the parent terminated
and reaped its isolated process group. Actual records/stdout remain in
`build/runtime-tests/{safe,unsafe,deadlock}.{jsonl,stdout}`. UBSan runs use the
separate `build/runtime-tests-ubsan/` directory.

Remaining scope limits: historical-request false positives, unknown static types,
bounded graph/tracker/registry capacities, synchronous sink backpressure,
unsupported timed/try/condition/interprocess/robust semantics, and no full
production coverage or performance benchmarks. Forced dlsym failure, asynchronous
cancellation, arbitrary preload combinations, and runtime ASan/TSan were not
validated. The stream prepares Phase 5; no backend/frontend was added.

## Phase 5 verification — 2026-10-26 IST

The real local FastAPI backend is implemented and verified. All requested Phase
1–5 regression commands passed. The final backend suite has 12 passing tests
using actual monitored banking processes and real HTTP/WebSocket sockets.
Safe: 22 records, zero warnings; unsafe: 23 records, one C warning; bounded
deadlock: 12 records, one warning, verified fixture evidence and process cleanup.
Separate ASan/UBSan, TSan, and runtime UBSan targets also passed.
See [Phase 5 verification](PHASE5_VERIFICATION.md) for dependencies, exact commands,
actual results, evidence locations and limitations. Earlier sections retain their
historical phase-specific scope. Phases 6 and 7 remain pending.

## Phase 6 verification — 2026-10-26 IST

The React/TypeScript dashboard is implemented with real FastAPI/C integration.
27 frontend tests, production build/type-check, and 3 real Chrome tests passed.
Browser tests covered all three banking programs, C warning/graph evidence,
replay at the actual warning event, report export, disconnect/reconnect and UI stop.
All Phase 1–5 regression commands also passed, including 12 backend tests.
See [Phase 6 verification](PHASE6_VERIFICATION.md) for actual versions, screenshots,
run evidence, exact commands and limitations. Final Phase 7 verification is recorded separately in FINAL_TEST_REPORT.md.
