# Final verification — 2026-10-26

All seven phases are delivered and verified within the local Linux/WSL scope. The final mandatory gate passed after a clean rebuild and the Phase 7 reliability changes. Preserved output: [check-final.log](../evidence/final/check-final.log). Tests use real programs where stated; frontend unit fixtures are explicitly test-only.

| Check | Actual result |
| --- | --- |
| Phase 1 | Two real workers print once and join successfully |
| Phase 2 | 20 safe + 20 unsafe runs; balances/orders, invalid inputs, limits, direct-launch guard, three bounded deadlock runs and SIGINT/SIGTERM cleanup passed |
| Phase 3 | Graph, tracker, analyzer and concurrent suites; twenty repeated concurrent runs (8 workers × 100 sequences) passed |
| Phase 4 | Real preload transparency, recursive depth, failed calls/errno, duplicate suppression, address generations, framing, TLS, cancellation, unknown types, capacities, unsupported operations and sink failures passed; bounded deadlock/signal cleanup passed |
| Phase 5 | 16 backend unit/integration tests passed |
| Phase 6 | TypeScript/Vite production build and 30 frontend unit/component/connection tests passed |
| Real browser | Three Chrome end-to-end journeys passed; no page errors in main integrated journey |
| Detection matrix | 14 real expected-versus-observed cases passed |
| Benchmarks | 144 validated runs, 24 warmups excluded from medians; 16 summary rows; exporter recomputed summaries from raw data |
| Analyzer ASan/UBSan | Four suites passed; [log](../evidence/final/asan-ubsan.log) |
| Concurrent analyzer TSan | Passed; [log](../evidence/final/tsan.log) |
| Runtime UBSan | Full runtime suite passed; [log](../evidence/final/runtime-ubsan.log) |
| Helgrind | Not run: valgrind is not installed |

The first clean final run passed, but its /tmp log disappeared when WSL stopped. The gate was rerun to durable repository logs, then rerun after final UI/evidence assertions. No failed result was substituted with an older passing result. The clean rebuild regenerated the C artifacts. Benchmark results and earlier Phase 6 evidence were preserved outside build/ before cleaning.

## Reliability fixes and checks

Cancelled supervisors now end as stopped with supervisor_cancelled and incomplete ingestion after cleanup, releasing their active slot. A regression cancels a real running fixture and verifies child/group absence. A SIGKILL test verifies failed exit -9, incomplete ingestion and cleanup. Two simultaneous controlled fixtures exhaust the default slots; a third is rejected. Twelve consecutive alternating safe/unsafe runs preserve correct warnings and stable descriptor usage. A fresh manager has no previous sessions.

Existing backend tests cover local API validation, origin/host boundary, real HTTP/WS delivery, late snapshots, timeout, stop, missing dependencies, launch failure, empty stream, malformed/oversized/partial records, output/history limits, projection bounds, slow queues, live snapshot recovery and shutdown with active children. These are bounded local tests, not a long-duration production soak or memory profiler.

Frontend replay now invalidates all reconstructed thread observations after sequence loss, including threads last seen before the gap. Accepted-index loss and slow-consumer errors mark retained history gaps. Terminal stream closure reloads the authoritative retained snapshot; failed reload remains visibly disconnected. Regression tests cover these cases and preserve interruption notices. This does not promise recovery of evicted backend history.

## Browser evidence

The main journey starts real safe, unsafe and controlled-deadlock processes, receives monitor WebSocket envelopes, inspects graph/cycle styling and thread details, filters the timeline, replays the warning, exports JSON and compares it to REST evidence. Session-list equality before/after replay verifies no new execution. It checks 16 saved performance rows from the 144-run set. The deterministic banking fixtures have 22 safe, 23 unsafe and 12 pre-timeout deadlock events; these are fixture assertions, not universal runtime counts.

Other journeys verify unavailable backend/missing-session states, real browser disconnect/reconnect, UI stop/reaping and retained-session reload. Chrome widths 1366, 1920 and 390 were exercised; narrow layout has no page-level horizontal overflow. Screenshots cover Overview, beginner help, Live Monitor, graph, threads, alerts, timeline, replay, sessions, reports, performance, safe completion and controlled timeout. Graph/report/performance/mobile and final scenario screenshots were visually reviewed. There is no formal accessibility or cross-browser audit.

Preserved artifacts: [screenshots](../evidence/final/screenshots/), [actual session export](../evidence/final/real-session-evidence.json), [cleanup/source audit](../evidence/final/audit.json), [SHA256 manifest](../evidence/final/manifest.json). The audit verifies ports 8000/5173 are free, no owned workload/server process remains and the original monitor engine files are unchanged.

## Changed files

New measurement/evaluation tools: `benchmarks/benchmark.c`, `benchmarks/run_benchmarks.py`, `benchmarks/export_results.py`, `scripts/evaluate_detection.py`, `scripts/check_final.sh`, `scripts/collect_final_evidence.py`. New reliability tests: `tests/backend/test_final_reliability.py`; runtime fixtures extend `tests/test_runtime_mutex.c`. Makefile adds benchmark, detection and final targets.

Backend fix: `backend/app/session_manager.py`. Frontend changes: `src/lib/replay.ts`, `src/lib/stream.ts`, `src/hooks/useSession.ts`, `src/api/types.ts`, `src/components/Report.tsx`, new `DemoHelp.tsx` and `pages/Performance.tsx`, App/styles, related tests and browser evidence assertions. `frontend/public/benchmark-*` contains verified static measurements. README and architecture/runtime/schema/user guides now reflect implemented phases; historical reports are labelled as historical.

Nine final documents cover overview, architecture, tests, performance, detection, demo, troubleshooting, limitations and presentation/viva. The source audit lists changed preexisting files; new files are described above. Git contained no tracked source baseline, so SHA256 comparison against the preserved pre-Phase-7 source manifest is used instead of claiming a meaningful git diff. No commit/push/publication occurred.

## Reproduce

From Ubuntu/WSL at the repository root, after documented dependency installation:

```bash
make clean
make check-final
make check-phase3-sanitize
make check-phase3-tsan
make check-phase4-ubsan
make benchmark
python3 benchmarks/export_results.py benchmarks/results/<new-completed-directory>
```

Stop development servers first. Benchmarking and sanitizers are separate from the mandatory gate. For preservation, redirect logs to evidence/final/ and run `python3 scripts/collect_final_evidence.py` only after the final gate finishes. See [demo guide](FINAL_DEMO_GUIDE.md) for launch commands, [performance](PERFORMANCE_EVALUATION.md) for measured overhead and [limitations](FINAL_LIMITATIONS.md) for boundaries.

Assessment: suitable for a local university submission and live demonstration with documented caveats. No unresolved critical blocker was found in this scope. Substantial logging overhead, unsupported synchronization semantics, bounded/memory-only history and lack of Helgrind comparison remain explicit limitations.
