> Historical phase checkpoint. Current completion and checks are in [FINAL_TEST_REPORT.md](FINAL_TEST_REPORT.md). Statements about later phases describe the time of this checkpoint.

# Phase 6 verification — 2026-10-26 IST

Phase 6 is complete for the local dashboard scope. A real React/TypeScript frontend
is integrated with the existing C/FastAPI implementation. No backend or C source
changes were required. Phase 7 has not been started.

## Installed versions

Linux Node v24.22.1 and npm 11.19.1 were installed locally under ignored .tools
using the official Node archive and SHA256 manifest. No privileged installation
was performed. Direct frontend packages are pinned and package-lock.json records
the resolved tree:

| Package | Installed version |
| --- | --- |
| React / React DOM | 19.3.1 |
| TypeScript | 7.0.2 |
| Vite / React plugin | 8.3.5 / 6.1.2 |
| Tailwind CSS / Vite plugin | 4.3.3 / 4.3.3 |
| @xyflow/react | 12.12.0 |
| lucide-react | 1.61.0 |
| lossless-json | 4.3.3 |
| Vitest | 5.0.5 |
| Testing Library React / jest-dom / user-event | 16.3.3 / 7.0.1 / 14.6.8 |
| jsdom | 30.1.3 |
| React / React DOM types | 19.3.0 / 19.3.0 |
| Playwright test | 1.64.1 |

Package versions/engine compatibility were inspected from the official npm registry
before installation. No charting library or router dependency was needed. A pinned
Prettier invocation formatted source; it is not a runtime dependency.

## Build and unit/component results

`npm run build` passes TypeScript checking and Vite production compilation.
Observed production output: approximately 455 kB JavaScript (144 kB gzip) and
38 kB CSS (8.5 kB gzip). These are build artifact sizes, not runtime benchmarks.

`npm run test` / `make check-phase6`: **27 tests pass in 3 test files**:

- 12 data/API cases: exact uint64 parsing, initial snapshots, deduplication, gaps,
  replay seeking, C-only warnings, recursive/incomplete observations, graph mapping,
  export evidence, create contract, history pagination and HTTP errors.
- 13 component/shell cases: catalog, navigation, no-session/error states, alerts,
  threads, timeline filters/details, replay controls/speed/truncation, timeout
  outcome, session history, report output and export control.
- 2 connection cases: subscribe-before-start, start once, unmount cleanup,
  session switching and rejecting stale identities.

These use explicit unit fixtures. React Flow rendering is exercised separately
by the real browser suite; component tests do not pretend to prove live integration.

## Real Chrome, Linux backend and C monitor

`make check-phase6-e2e`: **3 browser tests pass**. The managed runner starts fresh
loopback-only FastAPI and Vite servers, uses installed Windows Chrome through
Playwright against the Linux/WSL servers, and stops owned servers afterward.
Occupied ports cause a clear refusal rather than killing an unrelated server.

The first test launches all three approved programs via actual UI clicks:

| Scenario | Real UI result | Events | Warnings | Evidence |
| --- | --- | ---: | ---: | --- |
| Safe banking | completed, exit 0 | 22 | 0 | 2 graph nodes, 1 directed edge, no-warning alert state, cleanup verified |
| Unsafe banking | completed, exit 0 | 23 | 1 | 2 opposite directed edges, both C-cycle highlighted, exact warning path, cleanup verified |
| Controlled deadlock | timed_out, exit -9 | 12 | 1 | expected-timeout label, fixture_verified true, cleanup verified |

Unsafe warning evidence: sequence 16, closing edge lock_2 -> lock_1, cycle
[lock_1, lock_2, lock_1], graph_sequence 2. The browser compares the displayed path
and exported warning with the actual API response. In replay, position 15 has no
warning, advancing to 16 reveals it. Play/pause and seeking work against retained
real events. JSON download contains all 23 unsafe events and the real warning.
Node selection, thread selection and timeline filtering are also exercised.
Actual runtime_event WebSocket frames were observed, not mocked stream data.

The second test verifies missing-session and unavailable-backend messages, without
fake healthy metrics. The third disconnects the real active monitor socket,
verifies recovery with an explicit continuity notice, waits for the bounded timeout,
then launches another fixture and stops it using the UI. The stop result has
cleanup_verified true; reloading the page restores the terminal recorded session.

No browser page errors occurred in the main real-data scenario. Screenshots were
captured at 1366x768, 1920x1080 and 390x844. Narrow viewport has no page-wide
horizontal overflow, and its navigation toggle works. Visual inspection corrected
navigation scroll restoration and overlapping reverse-edge routing, then the
browser suite passed again with distinct opposite arrowheads.

## Evidence and reproducibility

Generated, ignored files from the latest run:

- frontend/test-results/real-session-evidence.json — actual safe/deadlock responses,
  unsafe downloaded report, observed monitor frame count and browser error list.
- frontend/test-results/overview-empty.png
- frontend/test-results/unsafe-live-1366.png
- frontend/test-results/unsafe-replay.png
- frontend/test-results/deadlock-timeout.png
- frontend/test-results/desktop-1920.png
- frontend/test-results/mobile-390.png
- build/phase6/backend.log and frontend.log — managed-server logs including shutdown.
- build/phase6/regressions.log — full prior-phase and frontend gate output.
- build/phase6/final-frontend-checks.log — final build/type-check and 27 passing tests after the navigation accessibility and session-ordering edits.
- build/phase6/final-browser-checks.log — all 3 real Chrome scenarios passing against that final source.

These contain run-specific PIDs/UUIDs/timestamps and are not production fixtures.
Playwright replaces test-results on rerun; make clean removes build evidence.
The runner reaps its Linux servers; browser test contexts close their Chrome pages.
No persistent dev server or monitored banking child was left running.

## Regression commands actually executed

All passed:

```bash
make check-phase2
make check-phase3
make check-phase4
make check-phase5
make check-phase6
bash scripts/check_phase1.sh
make check-phase6-e2e
```

Phase 5 retains its **12 passing backend tests**. Phase 3 concurrent repetition
and Phase 4 real preload/runtime lifecycle checks passed unchanged. No new
sanitizer or performance claims are made for Phase 6.

## Limitations and Phase 7 boundary

No known failing acceptance check remains. Chrome was verified; other browsers
and formal accessibility certification were not. In-memory sessions disappear
on backend restart. Reconnect is bounded recovery, not guaranteed durable replay.
HTTP projections can briefly lag stream events; separate resources are not an
atomic global state. Replay cannot reconstruct missing events, unsupported mutex
APIs, scheduler state or source lines. Its speed is illustrative, not execution
time. Bounded grid layout is suitable for the current teaching workloads; very
large graphs were not performance-benchmarked. No remote hosting, persistence,
authentication, automatic prevention, fabricated metrics or extra detector exists.

The implemented dashboard is ready to be an input to Phase 7 verification,
benchmarking and presentation work when requested. Phase 7 remains pending.
