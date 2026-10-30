# Deadlock Sentinel

Real supported pthread mutex monitoring, historical C lock-order analysis, FastAPI/WebSocket delivery and a React investigation dashboard for a university Operating Systems project.

All seven phases are delivered. See [final tests](docs/FINAL_TEST_REPORT.md) for actual verification. A potential cycle is **not proof of a current deadlock**. Only the controlled banking fixture has additional protocol/futex evidence. No production-readiness, prediction-accuracy or low-overhead claim is made.

## Install and launch in Ubuntu/WSL

From the repository root, with GCC, GNU Make, Bash and Python 3 (verified: 3.14.4):

```bash
make backend-install
python3 scripts/install_node.py  # optional local Linux Node bootstrap
make frontend-install
make monitor-build banking-build
```

Use two terminals at the repository root:

```bash
# Terminal 1
make backend-run
# Terminal 2
make frontend-run
```

Open **http://127.0.0.1:5173**; API documentation is at **http://127.0.0.1:8000/docs**. Stop each server with Ctrl+C. Use one backend worker; the default local workspace persists bounded sessions, projects and builds. Installation needs internet; installed demo assets work locally. See [backend setup](backend/README.md), [frontend setup](frontend/README.md) and [demo guide](docs/FINAL_DEMO_GUIDE.md).

## Your Code workspace

Open **Your Code** to paste C source or upload up to eight `.c`/`.h` files. Save an
immutable revision, **Build**, then **Run with Monitoring**. Inspect real output,
graphs, warnings, timelines, replay and exports. Editing source makes its build
stale. Compiler errors prevent running that build. **Stop build** and **Stop run**
are separate actions. Built-in workloads remain under **Overview > Examples**.

Compilation and execution require Bubblewrap plus a systemd user manager with
cgroup v2 memory, pids and CPU controls. The backend probes the real isolation
provider at startup and disables custom Build/Run when unavailable; it never
falls back to host execution. The sandbox has no host network, capabilities or
credentials, a read-only runtime, bounded scratch, and CPU/memory/process/output
limits. This is a local shared-kernel boundary, not a remotely deployable service.

See [Your Code setup and user manual](docs/YOUR_CODE.md) for exact prerequisites,
API workflow, all resource/retention limits and the two submitted-source demos.
The default `.workspace/` store is local and Git-ignored. Delete terminal sessions
or projects explicitly to reclaim quota. Replay is recorded history, not execution.
Source call-site resolution is currently unavailable. Same-process monitor events
are validated but are not tamper-proof. Zero warnings never prove safety.

## Verify and measure

Stop your development servers before managed browser checks, which reject occupied ports.

```bash
make check-final
make check-phase3-sanitize
make check-phase3-tsan
make check-phase4-ubsan
make benchmark
python3 benchmarks/export_results.py benchmarks/results/20261026T094025.091439Z
```

For a new benchmark, export its newly printed results directory. `check-final` runs all phase gates, detection evaluation and real browser integration. Browser prerequisites are in frontend/README.md. `make clean` removes generated build/; preserved evidence and benchmarks remain outside it. `make help` lists individual targets.

## Delivered phases and evidence

1. Foundation: real two-thread program and GCC/Make checks.
2. Banking: safe, sequential unsafe and bounded controlled deadlock.
3. Analysis: held-lock tracking, historical graph and BFS cycle paths.
4. Runtime: real Linux LD_PRELOAD interception and JSONL.
5. Backend: bounded supervision, REST, WebSockets and projections.
6. Dashboard: graph, alerts, threads, timeline, recorded replay and reports.
7. Final integration: lifecycle fixes, detection matrix, real benchmarks, saved Performance view, evidence and presentation documents.

The [performance evaluation](docs/PERFORMANCE_EVALUATION.md) contains 144 actual runs including 24 warmups. Full synchronous logging is expensive on tiny critical sections. Both file and /dev/null measurements retain analysis and JSON formatting. The dashboard labels these saved measurements, not live session estimates.

The [detection evaluation](docs/DETECTION_EVALUATION.md) compares expected and observed behavior without an accuracy score. Safe and unsafe banking finish at 105000/95000 cents, total 200000. Unsafe warns but completes. The deliberate deadlock is killed/reaped after 2.5 seconds and has an incomplete normal ending. Never run that fixture directly; use `make monitor-deadlock` or its approved dashboard entry.

## Project and documentation

`examples/` holds real C workloads; `monitor/` holds analyzer and hooks; `backend/app/` owns sessions; `frontend/src/` renders evidence; `tests/` and frontend tests verify behavior; `benchmarks/` holds raw measurement results; `evidence/` preserves actual verification artifacts.

- [Final overview](docs/FINAL_PROJECT_OVERVIEW.md) and [architecture](docs/FINAL_ARCHITECTURE.md)
- [Final tests](docs/FINAL_TEST_REPORT.md), [performance](docs/PERFORMANCE_EVALUATION.md), [detection](docs/DETECTION_EVALUATION.md)
- [Demo guide](docs/FINAL_DEMO_GUIDE.md), [presentation and viva](docs/FINAL_PRESENTATION_SCRIPT.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md), [limitations](docs/FINAL_LIMITATIONS.md)

The monitor supports a bounded subset of Linux pthread behavior, does not prevent deadlocks and cannot prove arbitrary programs safe. Fixed capacities, unsupported APIs, synchronous logging, scheduling effects and bounded retention are explicit limits. Your Code accepts bounded C/H source uploads and uses a fail-closed Linux sandbox. No remote deployment, authentication, executable upload or arbitrary command launcher is provided.
