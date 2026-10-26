> Historical phase checkpoint. Current completion and checks are in [FINAL_TEST_REPORT.md](FINAL_TEST_REPORT.md). Statements about later phases describe the time of this checkpoint.

# Phase 5 verification — 2026-10-26 IST

Phase 5 is complete for local Linux/WSL. Phase 6 React and Phase 7 full-system
benchmarking remain pending. C sources and the event schema were preserved.

## Actual API results

| Application | Terminal result | Accepted records | C warnings | Cleanup |
| --- | --- | ---: | ---: | --- |
| banking_safe | completed, exit 0 | 22 | 0 | PID reaped, group absent |
| banking_unsafe | completed, exit 0 | 23 | 1 | PID reaped, group absent |
| banking_deadlock | timed_out, exit -9 | 12 | 1 | killed at 2.5-second bound, reaped, group absent |

Safe/unsafe had zero invalid records, no truncated history, complete C/ingestion
flags, and final balances 105000 + 95000 = 200000 cents. The unsafe C warning has
type potential_lock_order_cycle, sequence 16, closing_edge [lock_2, lock_1],
cycle [lock_1, lock_2, lock_1], graph_sequence 2. HTTP alerts were compared against
the complete actual WebSocket warning objects, preserving identities and timestamps.
Graph edges were compared against the real held-at-request relationships.

Controlled-deadlock evidence includes both conflicting requests, both held-lock
and request stdout markers, the C warning and three /proc futex waiters before
kill. fixture_verified is true. Missing session_end after SIGKILL correctly makes
ingestion_complete false even though C-reported completeness is true. This
fixture-specific observation does not make historical cycles confirmed deadlocks.

## Checks executed

The complete requested sequence in scripts/verify_phase5_regressions.sh passed:

```bash
make clean
make
make run
make banking-safe
make banking-unsafe
make check-phase2
make analysis-demo
make check-phase3
make monitor-safe
make monitor-unsafe
make check-phase4
make check-phase5
bash scripts/check_phase1.sh
```

The full regression run passed the initial 10 Phase 5 tests. Additional shutdown
and queue-recovery cases were then added; the final Phase 5 suite passes **12 tests**.
These grouped tests contain multiple scenarios and assertions. Real Uvicorn HTTP
and WebSocket sockets launch actual monitored GCC banking programs.

Coverage: application import, health/catalog, validation and local boundary,
safe/unsafe runs/events/warnings, graph/thread views, two live subscribers plus an
independently disconnected client, late running/terminal snapshots, deadlock
timeout/stop, real server shutdown with an active child, PID/group absence,
repeated-run FD counts, retention gaps, concurrency/retention limits, missing
dependencies, failed spawn, empty streams, malformed/partial JSONL, projection
semantics and bounded slow queues. Queue saturation uses real events and a
deliberately undrained internal subscriber; it is not a browser congestion benchmark.

Separate optional checks all passed:

- make check-phase3-sanitize: AddressSanitizer/UndefinedBehaviorSanitizer suites.
- make check-phase3-tsan: concurrent analyzer ThreadSanitizer check.
- make check-phase4-ubsan: real monitored runtime regression suite.
- backend/.venv/bin/python -m pip check: no broken requirements.

No ASGI disconnect exceptions remained after correcting close handling. Final
process inventory contained no banking process or Uvicorn test server. Tests manage
their server/child lifecycles; no persistent development server was left running.
Real API evidence is in build/backend-tests/banking_safe.json and banking_unsafe.json.
Regression/sanitizer logs are copied into build/backend-tests. This is ignored,
regenerable build output removed by make clean; PIDs/timestamps vary per run.

## Installed environment

Ubuntu/WSL, Python 3.14.4, local pip 26.2.2. Verified installed packages:

```text
annotated-doc==0.0.5
annotated-types==0.8.0
anyio==4.15.1
click==8.5.0
fastapi==0.162.0
h11==0.16.0
idna==3.20
opentelemetry-api==1.45.1
pydantic==2.14.1
pydantic_core==2.50.1
starlette==1.10.0
typing-inspection==0.4.4
typing_extensions==4.16.0
uvicorn==0.54.0
websockets==17.3
```

FastAPI, Uvicorn, Pydantic and websockets are direct pinned dependencies. Others
are transitive packages. No telemetry exporter or external service is configured.
The installer uses the ignored backend/.venv only; normal execution is offline.

## Limits of verification

No React/browser visual verification, durable delivery, database, remote monitoring,
authentication, production deployment or performance benchmark is claimed. Sessions
are lost on restart. Monitor scope remains the existing supported pthread APIs.
Thread views are last observed, not exact scheduler state. See API_CONTRACT.md and
BACKEND_ARCHITECTURE.md for bounds, completeness flags, security and wire semantics.
Phase 6 can consume the verified REST/WS interfaces; it has not been started.
