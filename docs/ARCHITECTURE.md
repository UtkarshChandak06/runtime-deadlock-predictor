# Architecture: implemented system

Phases 1–6 are complete: C workloads, analysis, interception, JSONL, a real local FastAPI/WS backend, and a React dashboard. Phase 7 integration and benchmarking are documented in FINAL_TEST_REPORT.md and PERFORMANCE_EVALUATION.md. See [Frontend architecture](FRONTEND_ARCHITECTURE.md) for live streaming, graph rendering and recorded-session replay. See [Backend architecture](BACKEND_ARCHITECTURE.md) for the implemented process supervisor and projections:

```text
C application -> C runtime monitor -> graph analysis -> event transport
              -> FastAPI backend -> React dashboard
```

| Component | Responsibility | Status |
| --- | --- | --- |
| C application | Run banking transactions across real threads | Phase 2 complete: safe, sequential unsafe, controlled deadlock workloads |
| C runtime monitor | Use LD_PRELOAD to observe supported POSIX mutex operations | Phase 4 complete; lock/unlock/init/destroy hooks |
| Graph analysis | Track held locks, maintain historical request dependencies, reconstruct BFS cycle paths | Phase 3 complete; API reused by Phase 4 |
| Event transport | Carry structured real runtime events through a dedicated descriptor | Phase 4 JSONL v1 and Phase 5 inherited-FD consumer verified |
| FastAPI backend | Manage monitoring sessions and deliver live events/reports | Phase 5 complete; bounded local REST/WS service |
| React dashboard | Display the graph, threads, timeline, warnings, and session reports | Phase 6 complete; real REST/WS integration, graph, investigations, replay and reports |

Graph analysis lives within the monitor; the arrows describe responsibilities,
not necessarily separate processes. Phase 7 tests the complete system and records measured overhead with explicit workload and environment provenance.

A lock-order graph describes acquisition patterns. A cycle is a potential-deadlock
warning, even if the application finishes normally. It does not by itself prove
threads are currently deadlocked. Future labels and reports must preserve this
distinction. No detection accuracy or performance claims are made in Phase 1.

The Phase 1 example passes stable IDs to two workers and joins them. Phase 2 adds
per-account mutexes protecting shared balances. Console markers describe actual
fixture operations; they are not a runtime monitoring event service. The Python
parent verifies the known deadlock fixture using its source-defined locking
protocol, successful-lock/request markers, Linux futex wait states, and a timeout.
This is not a general graph detector. See BANKING_SIMULATOR.md for details.

Phase 3 now supplies `libdeadlock_analysis.a`: independent tracker, graph/BFS,
and analyzer modules under `monitor/`. Each tracker and the shared graph have
their own internal mutex; the analyzer snapshots tracker state before entering
graph synchronization and never nests these locks. Caller-owned warning copies
are formatted after analysis returns. Fixed capacities and sticky incomplete
flags make dropped evidence visible. No banking source modifications were added.
See GRAPH_ANALYSIS.md for the public API, lifecycle rules, recursion policy,
request-based false positives, and the Phase 3 API boundary.

Phase 4 adds `hooks.c` (forwarded pthread calls), `runtime.c` (TLS, address
generations, shared graph, failure policy), and `events.c` (JSONL sink). A TLS guard
excludes internal mutexes. Registry, graph/tracker, and output mutexes are not
nested across modules; the real application lock executes with none held.
Initialization/destruction hooks obtain public type metadata and retire identities.
Records use actual Linux PID/TID values and independent event/graph sequences.
See RUNTIME_MONITOR.md and EVENT_SCHEMA.md for scope and the Phase 5 contract.
