# Presentation script and viva

## Five-minute script

**0:00–0:40:** “Two threads can each hold one mutex and wait for the other. We observe real pthread operations, record historical lock ordering and warn when a request closes a cycle. A historical cycle is risk, not necessarily current deadlock.” Show beginner help.

**0:40–1:20:** “The C preload library analyzes calls. JSONL flows through a dedicated pipe to a bounded FastAPI supervisor; WebSockets carry evidence to React. The browser displays C warnings.” Show architecture.

**1:20–2:00:** Run Safe then Unsafe. “Both finish with the correct total. Unsafe has opposite historical orders but sequential workers, so it warns without deadlocking.”

**2:00–2:50:** Open Alerts, Graph, Threads and Timeline. Step Replay across the warning. “This is recorded evidence, not another process execution.”

**2:50–3:35:** Launch Controlled deadlock. “A barrier deliberately coordinates conflicting requests. The supervisor checks fixture markers and futex waits, then kills and reaps the group after 2.5 seconds. Missing normal end metadata correctly makes ingestion incomplete.”

**3:35–4:20:** Open Performance. “We used the same C binary, one warmup and five measured repetitions. Raw CSV and environment metadata are available. Full logging is expensive for tiny critical sections; these results do not justify calling the monitor lightweight.” Read a row's actual milliseconds.

**4:20–5:00:** Show Reports and final tests. “This is a tested local educational tool with bounded queues and explicit incomplete evidence. It does not prevent deadlocks, cover every pthread operation or persist sessions across restart.”

## Ten-minute outline

Spend one minute on motivation/Coffman conditions; two on hooks, held sets, request edges and BFS; two on safe/unsafe and replay; one on controlled deadlock and cleanup; one on measurement/method; one on reliability tests; one on limitations and one on questions. Keep Stop/reload as an optional extra demonstration.

## Viva

- **Why does unsafe finish?** Workers use opposite orders but are joined sequentially; history does not imply simultaneous waiting.
- **Why request before acquire?** A blocked lock call may never return. Requests expose risk early but failed requests can leave conservative historical edges.
- **What confirms the controlled deadlock?** Known barrier protocol, successful first locks, opposite requests, C warning, futex waits and bounded lack of progress. This is fixture-specific.
- **Does no cycle prove safety?** No: unsupported APIs, incomplete data and unexecuted paths remain unknown.
- **Why BFS?** Find a path back from the proposed edge destination to its source and return a concrete cycle.
- **Recursive locks?** Explicit recursive depth is tracked. Unknown static kinds remain conservative.
- **What is replay?** Recorded-prefix projection without another process or independent cycle detector.
- **How are orphans prevented?** Owned process groups, timeouts, termination and wait/reap; regressions verify group absence after stop, cancellation, timeout and shutdown.
- **What is the overhead?** Use workload-specific saved medians and absolute costs. Default monitor logging is synchronous and unoptimized; measured overhead is high and host-specific.
- **What survives restart?** Exported files only; backend sessions are memory-only.
- **Future work?** Logging optimization with explicit loss semantics, broader synchronization support, more application workloads and durable storage. These are not delivered claims.

## Fallback

Keep `evidence/final/`, benchmark raw CSV and FINAL_TEST_REPORT.md locally. If live execution fails, show screenshots and real-session-evidence.json explicitly labelled as recorded verification. Terminal bounded targets can demonstrate the C engine without React. Do not substitute unlabelled mock events.
