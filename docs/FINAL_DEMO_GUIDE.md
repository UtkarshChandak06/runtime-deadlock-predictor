# Final local demo guide

Use the repository root in Ubuntu/WSL. Install once with `make backend-install`, optional `python3 scripts/install_node.py`, then `make frontend-install`. Build with `make monitor-build banking-build`.

In two separate terminals at the repository root:

```bash
# Terminal 1
make backend-run
# Terminal 2
make frontend-run
```

Open http://127.0.0.1:5173. API documentation is at http://127.0.0.1:8000/docs. Use one backend worker. Stop each server with Ctrl+C.

1. Open Overview and expand **Explain this demo** to explain threads, mutexes, historical edges and cycles.
2. Start **Safe banking**. Wait for completed, zero warnings and one-direction ordering. Reports shows final balances 105000/95000, total 200000, and verified cleanup.
3. Start **Unsafe lock ordering**. It completes with one potential cycle. Its sequential workers demonstrate why historical risk does not prove simultaneous deadlock.
4. Open Alerts, inspect the cycle in Graph, select a lock, open Thread Explorer and filter Event Timeline for the C warning.
5. Open recorded Replay. Step before/after the warning and play/pause. This reveals existing records without a new C launch. Terminal outcome and playback position are distinct.
6. Open Reports and export JSON. Inspect actual application metadata, outcome, evidence, completeness and continuity. Print provides a print-oriented layout.
7. Open Performance. Explain saved date/provenance, repetitions, baseline and monitored medians, absolute overhead and raw CSV. Both sinks retain full analysis/formatting; overhead is high on these tiny workloads.
8. Start **Controlled deadlock** from Overview. Observe warning then timeout at approximately 2.5 seconds. Show fixture-specific evidence and cleanup. Missing normal end metadata is expected after termination.
9. Optionally start it again and click Stop session. Reload to inspect retained stopped evidence. Backend restart clears sessions by design.
10. Stop both servers with Ctrl+C. Closing a browser tab alone does not stop the supervised process.

Never launch the deadlock executable directly. Terminal-only alternatives are `make monitor-safe`, `make monitor-unsafe` and `make monitor-deadlock` (bounded harness).

Before presenting, stop dev servers and run `make check-final`. The managed browser suite rejects occupied ports and cleans up its own servers. Preserved screenshots and real session exports are in `evidence/final/`; historical Phase 6 evidence is in `evidence/phase6-preserved/`. If live execution fails, explicitly identify those files as recorded verification evidence.
