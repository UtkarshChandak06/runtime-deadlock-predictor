# Final project overview

Deadlock Sentinel is a local university Operating Systems project. Real supported pthread calls feed a bounded C historical lock-order analyzer. JSONL travels through an inherited descriptor to FastAPI, which supervises processes and serves evidence over REST and WebSockets. React provides graph, threads, timeline, alerts, recorded replay, reports and saved benchmark measurements.

| Phase | Delivered behavior |
| --- | --- |
| 1 | GCC/Make environment and real two-thread smoke test |
| 2 | Safe, sequential unsafe and bounded actual-deadlock banking workloads |
| 3 | Held-lock tracking, synchronized graph and BFS cycle reconstruction |
| 4 | Linux LD_PRELOAD mutex interception and structured runtime events |
| 5 | Bounded process supervision, validated ingestion, REST and WebSockets |
| 6 | Real React dashboard and recorded-session investigation |
| 7 | Integration fixes, reliability checks, detection matrix, benchmarks and presentation package |

Current verification is documented in [FINAL_TEST_REPORT.md](FINAL_TEST_REPORT.md). Prior phase reports remain historical evidence. No production page injects synthetic monitor events. Unit tests use explicitly identified fixtures; browser tests and measurements use real processes.

Phase 7 fixes cancelled supervisors retaining a running state, stale replay reliability after sequence loss and incomplete terminal stream-history recovery. It adds beginner explanations, catalog and continuity metadata in reports, a saved Performance view, three/eight-lock cycle fixtures and repeatable measurement/evaluation tools. The C analyzer and runtime engine are retained.

The main contribution is integration and an honest educational demonstration, not a new general deadlock algorithm. A historical cycle is a potential risk, not proof of current deadlock. Only the controlled banking fixture has additional protocol/futex evidence supporting actual-deadlock confirmation. No prevention, future prediction accuracy, low-overhead or production-readiness claim is made.

Submit source, build/check scripts, frontend lockfile, backend requirements, final documents, raw benchmark results and preserved evidence. Generated environments, dependencies and binaries can be recreated. No commit, push or remote publication was performed.
