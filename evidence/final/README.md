# Final evidence index

Recorded on 2026-10-26; these are saved verification artifacts, not current live sessions.

- [Mandatory final gate](check-final.log): phase 1–6 checks, 14-case detection matrix and three real browser journeys.
- [Analyzer ASan/UBSan](asan-ubsan.log), [concurrent analyzer TSan](tsan.log), [runtime UBSan](runtime-ubsan.log).
- [Production build](production-build.log), [actual browser session evidence](real-session-evidence.json), [server log](browser-backend.log).
- [Cleanup and source audit](audit.json): free ports, absent owned processes, unchanged monitor engine.
- [SHA256 manifest](manifest.json): file sizes and hashes for preserved evidence.
- [Benchmark raw data and environment](../../benchmarks/results/20261026T094025.091439Z/).
- [Final detection matrix](../detection/20261026T143514Z/matrix.csv) and per-case real JSONL/stdout in that directory.

## Screenshots

| Evidence | Screenshot |
| --- | --- |
| Overview before running | [overview-empty](screenshots/overview-empty.png) |
| Beginner explanation | [beginner-help](screenshots/beginner-help.png) |
| Safe completion | [safe-completed](screenshots/safe-completed.png) |
| Unsafe real session | [unsafe-live](screenshots/unsafe-live-1366.png) |
| Live Monitor section | [live-monitor](screenshots/live-monitor.png) |
| Selected graph evidence | [graph](screenshots/graph.png) |
| Thread details | [threads](screenshots/threads.png) |
| C-reported warning | [alerts](screenshots/alerts.png) |
| Filtered warning event | [timeline](screenshots/timeline.png) |
| Recorded replay | [unsafe-replay](screenshots/unsafe-replay.png) |
| Session list | [sessions-desktop](screenshots/sessions-desktop.png) |
| Report and real stdout | [reports](screenshots/reports.png) |
| Saved measurements | [performance](screenshots/performance.png) |
| Bounded actual-deadlock fixture | [deadlock-timeout](screenshots/deadlock-timeout.png) |
| Wide desktop | [desktop-1920](screenshots/desktop-1920.png) |
| Narrow session list | [mobile-390](screenshots/mobile-390.png) |

Tables intentionally scroll inside their panels; full-page screenshots show the current scroll position. JSON/CSV/log artifacts contain the complete retained evidence. Historical Phase 6 artifacts are separately preserved under evidence/phase6-preserved/.
