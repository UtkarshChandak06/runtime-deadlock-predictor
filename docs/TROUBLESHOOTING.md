# Troubleshooting

| Symptom | Check and remedy |
| --- | --- |
| Build/pthread errors in PowerShell | Enter Ubuntu/WSL and run Make there; Windows GCC is not the verified runtime. |
| Missing backend environment | Run `make backend-install` from the repo with internet available. |
| Missing Linux Node or Windows npm selected | Run `python3 scripts/install_node.py`; Make frontend targets select local Node. |
| Backend unavailable | Start `make backend-run`, inspect its terminal and check http://127.0.0.1:8000/api/v1/health. |
| Occupied ports | Stop your own previous server. Tests refuse occupied ports rather than killing unrelated processes. |
| Origin rejected | Use http://127.0.0.1:5173 or http://localhost:5173. Keep documented ports and loopback bindings. |
| Missing session after restart | State is memory-only. Start another approved run; export evidence before restart. |
| Preload error with spaces | Rebuild monitor; use `LD_PRELOAD=./build/libdeadlock_monitor.so` from repo root. The loader splits absolute paths containing spaces. Backend uses a private space-free copy. |
| Unknown/incomplete status | Inspect C scope, ingestion, truncation, sequence loss, unsupported kinds/APIs and terminal exit separately. Do not hide missing evidence. |
| Controlled timeout | Expected at 2.5 seconds. Check fixture evidence and cleanup; timeout alone is not deadlock proof. |
| Disconnected stream | Retry/reload retained evidence. Evicted records cannot be reconstructed; continuity notices remain meaningful. |
| No saved benchmarks | Run `make benchmark`, then export its completed directory with `python3 benchmarks/export_results.py benchmarks/results/<directory>`. |
| Browser unavailable | See frontend/README.md. WSL uses installed Windows Node/Chrome when available; native Linux needs Playwright Chromium and OS dependencies. |
| Futex evidence missing | The fixture requires readable Linux /proc task wchan. Restricted-host failures are inconclusive. |
| Huge percentages | Inspect absolute durations and raw CSV. Short baselines plus full synchronous logging produce large ratios. |

`build/` logs are overwritten by checks or removed by `make clean`. Preserved final evidence lives outside build/. WSL /tmp may disappear after distro shutdown, so final logs are stored in the repository. Do not kill unrelated processes to fix a local test failure.
