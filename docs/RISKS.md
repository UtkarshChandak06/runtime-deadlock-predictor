> Historical planning document. Current implementation and verified results are in ARCHITECTURE.md and PHASE4_5_VERIFICATION.md; planned features below are not claims of support.

# Technical Risks and Mitigations

**Project:** Runtime Deadlock Prediction Using Lock-Order Graph Analysis
**Phase:** 0 — Planning

Each risk lists why it matters, how it would show up, and the concrete
mitigation built into the design. R1–R4 are the four areas required by the
Review 1 task; the rest are additional hazards we identified.

---

## R1. Thread Safety of Monitor State

**Why it matters.** The lock-order graph, mutex registry, and warning
deduplication state are shared by all application threads and mutated on
every lock acquisition.

**How it would show up.** Corrupted adjacency lists, lost edges, warnings
printed twice or not at all — bugs that appear only under heavy contention
and are extremely hard to reproduce.

**Mitigations.**

- One global monitor lock protects **all shared monitor state** (graph +
  registry). With exactly one monitor lock there is no monitor-side lock
  ordering problem to get wrong.
- Per-thread held-lock sets live in thread-local storage (`__thread` /
  `pthread_key_t`), so the hottest path takes no shared lock at all.
- Every shared structure has exactly one owner module; no module reaches
  into another module's data (interfaces in ARCHITECTURE.md §3).
- A dedicated unit test hammers the graph from many threads while checking
  invariants (TESTING_PLAN.md, unit tests).

---

## R2. Recursive Interception (Reentrancy)

**Why it matters.** Our wrapper **is** `pthread_mutex_lock`. If anything the
wrapper itself calls (`dlsym`, `malloc`, `fprintf`, `pthread_self`
internals) takes a mutex, we re-enter our own wrapper — infinite recursion
and an immediate stack overflow, before the program does anything useful.

**How it would show up.** Crash at startup on the very first wrapped call;
or subtle recursion only on the logging path when `RDP_VERBOSE=1`.

**Mitigations.**

- Thread-local guard `__thread int rdp_in_hook`: while set, the wrapper
  forwards directly to the real function and does no bookkeeping.
- `dlsym(RTLD_NEXT, ...)` is resolved **once in a constructor** and cached;
  it is never called from the hot path (dlsym itself locks internally).
- Logging uses fixed-size stack buffers and single-shot `write()`-style
  output — no `malloc` and no stdio flushing inside the wrapped path.
- Rule: no module except `hooks.c` may call wrapped pthread functions; the
  monitor's own internals are pure computation.
- Verification: an integration test with `RDP_VERBOSE=1` must produce event
  traces and exit 0 — recursion bugs show up there first.

---

## R3. Graph Synchronization vs. Analysis Cost

**Why it matters.** Cycle detection is a DFS that must read the graph while
other threads are adding edges. Holding the monitor lock across an expensive
traversal blocks every other thread's bookkeeping and can dominate runtime
overhead — the very metric we must report.

**How it would show up.** Lock/probe microbenchmarks showing 10× overhead;
scalability test where throughput collapses as thread count grows.

**Mitigations.**

- DFS runs **only when a new (de-duplicated) edge is added**. Each unique
  edge is analyzed at most once, so total analysis work is bounded by the
  number of distinct lock-order pairs, not by the number of lock events.
- The monitor lock is a **leaf lock**: nothing that can block on an
  application lock (no real `pthread_*` calls, no stdio, no `malloc` while
  holding it). This makes it impossible for the monitor to participate in
  the deadlock it is observing.
- Adversarial case considered: one thread stuck in a very large graph DFS
  while others wait. Mitigation: edge de-duplication keeps graphs small in
  practice; a configurable graph-size cap degrades gracefully (open
  question, ARCHITECTURE.md §6).
- Measurement: the benchmark suite quantifies the worst case instead of us
  guessing (TESTING_PLAN.md, overhead metrics).

---

## R4. Blocking Mutex Calls and Wrapper Semantics

**Why it matters.** `pthread_mutex_lock` may block indefinitely. Any lock we
take around it — or any bookkeeping done at the wrong time — changes program
semantics or hides the deadlock we exist to detect.

**How it would show up.**

- If we record dependencies **after** the real call: when two threads are
  already deadlocked, neither acquisition ever completes, so the graph stays
  empty precisely when detection matters most.
- If we hold the monitor lock while calling the real lock: classic
  ABBA deadlock between monitor lock and application lock (thread 1 holds
  monitor lock, waits for app lock; thread 2 holds app lock, waits for
  monitor lock) — the tool deadlocks the program it is measuring.
- If we make trylock blocking, or drop errors/errno/timeout behavior, the
  application misbehaves under preload.

**Mitigations.**

- Wrapper order (ARCHITECTURE.md §3.6): snapshot held set → record intent
  edges + cycle check → **real call with no monitor lock held** → push into
  held set only on success. In-progress deadlocks are still visible.
- Faithful forwarding: return value, `errno`, and blocking behavior of the
  real function are passed through untouched (this is testable: every test
  program behaves identically with and without preload, apart from warnings).
- `trylock`: edges and held-set updates happen only when it succeeds;
  a failed trylock creates no dependency because no waiting occurred.
- `timedlock`: same path as lock, with the timeout result respected.
- Phase 4 handles `pthread_cond_wait` (mutex released and reacquired
  internally by the implementation) — until then it is a documented gap.

---

## R5. False Positives (Gate Locks, Ordered Hierarchies)

**Why it matters.** A graph cycle means "these orders were observed", not
"these threads can actually deadlock". Gate locks, try/finally idioms, and
one-time initialization locks routinely create infeasible cycles. Excess
false positives would invalidate our evaluation.

**How it would show up.** Test t05 (gate-lock pattern) warns although no
execution can deadlock; reviewers question the tool's usefulness.

**Mitigations.** Documented as a *measured* limitation, not hidden: the test
suite includes a deliberate false-positive case and evaluation reports a
false-positive rate alongside detection rate (TESTING_PLAN.md §5). Edge
first-observer thread IDs are kept in the warning so a human can judge
feasibility.

---

## R6. Deadlock or Livelock Inside the Monitor Itself

**Why it matters.** A deadlock detector that deadlocks is a total failure.

**Mitigations.** Single leaf monitor lock (R1, R3); recursion guard (R2);
no blocking operations under the monitor lock; a CI-style smoke test runs
the whole suite under `timeout` so a monitor self-deadlock fails the build
instead of hanging it.

---

## R7. Thread Lifecycle, Fork, and Cleanup

**Risks.** Thread-local held-lock lists must be freed at thread exit;
`fork()` in a multithreaded program carries monitor state into the child
where it may be inconsistent; atexit-time logging must not run after
destructors that already freed things.

**Mitigations.** `pthread_key_create` with a destructor frees per-thread
state; state is rebuilt lazily after `fork` (best-effort, documented);
constructor/destructor ordering kept in one file (`hooks.c`).

---

## R8. Interposition Coverage and Platform Limitations

**Risks.** glibc versioned symbols, statically linked binaries, direct
`futex` use, or an application that never calls the wrapped functions would
silently escape monitoring; the project requires Linux (the course
environment), so Windows builds are out of scope.

**Mitigations.** Verify interposition explicitly in Phase 1 with a
`RDP_VERBOSE=1` smoke test (if events do not appear, stop and fix before
building the graph); document that only dynamically linked, glibc/pthread
programs are supported; develop under WSL2/Ubuntu.

---

## R9. Measurement Validity (Overhead and Timing)

**Risks.** Noisy shared machines, CPU frequency scaling, and warm-up effects
can easily produce ±20% swings, making an honest overhead claim impossible.

**Mitigations.** `run_bench.sh` uses a fixed workload, warm-up iteration,
median of ≥5 runs, and reports baseline and monitored numbers side by side
rather than a single percentage; results table lives in `docs/RESULTS.md`.

---

## Risk Summary

| ID | Area | Severity | Primary mitigation | Phase |
|----|------|----------|--------------------|-------|
| R1 | Thread safety of monitor state | High | Single monitor lock + TLS held sets | 2–3 |
| R2 | Recursive interception | Critical | Reentrancy guard + one-time dlsym | 1 |
| R3 | Graph synchronization / cost | High | DFS only on new edges; leaf lock | 3 |
| R4 | Blocking mutex semantics | Critical | Edges before real call; monitor lock never held across it | 2–4 |
| R5 | False positives | Medium | Measure and report FP rate honestly | 5 |
| R6 | Monitor self-deadlock | Critical | Leaf-lock invariant + timeout'd test suite | 3 |
| R7 | Thread/fork lifecycle | Medium | TLS destructor, lazy rebuild after fork | 2–4 |
| R8 | Interposition coverage | Medium | Verbose smoke test before feature work | 1 |
| R9 | Measurement validity | Medium | Median of N runs, side-by-side baseline | 5 |

