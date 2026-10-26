# Deadlock Sentinel dashboard user guide

The dashboard helps explain how real Linux threads request and release locks.
The C monitor records the events, FastAPI supervises the program, and the browser
shows the resulting evidence. A historical lock-order cycle is a risk pattern;
it does not prove that an application is currently deadlocked.

## Start the local application

Run inside Ubuntu/WSL at the deadlock-sentinel repository root:

```bash
# First-time setup; Node bootstrap is only needed if Linux Node is missing.
python3 scripts/install_node.py
make frontend-install
make backend-install
```

Then use two terminals:

```bash
# Terminal 1
make backend-run
# Terminal 2
make frontend-run
```

Open http://127.0.0.1:5173 in your browser. The top bar should report a connected
backend, and Overview should list approved programs from the backend catalog.
If it is unavailable, start the backend and click Retry connection. Keep both
servers local. Press Ctrl+C in each terminal when finished.

## Safe and unsafe banking

On Overview, select Start Monitoring on **Safe banking**. The UI attaches to the
session before starting the real program. It usually finishes almost immediately.
The verified run shows completed, 22 events, two locks, two observed threads,
one dependency and zero potential-cycle warnings. These are measured example
results, not fixed UI values or proof of universal safety.

Run **Unsafe lock ordering** next. Its two workers acquire the same locks in
opposite orders at different times. It completes normally but the C analyzer reports
a potential lock-order cycle. Open Alerts: the warning shows the thread, event
sequence, closing dependency and full reported path. The verified run has 23 events
and one warning at event 16, with lock_1 -> lock_2 -> lock_1.

## Graph, threads and timeline

Lock Graph shows observed lock generations, not account balance charts. Arrows are
historical request dependencies. Red edges are part of C-reported cycles. Drag the
canvas to pan, scroll to zoom, or use the fit control. Select a lock or edge for
recorded metadata/evidence. Two opposite arrows remain separately visible.

Thread Explorer shows each thread's real ID and tracker instance. Select a card
to inspect recent operations. Read 'last observed request' literally: it does not
mean the browser knows the thread's current kernel state. Incomplete observations
are marked limited/unknown.

Event Timeline lists actual monitor sequence numbers and monotonic nanosecond
timestamps. Filter by event type, thread, or lock identity. Inspect an event to see
its original available fields. A request, acquisition, failed operation and release
are distinct events. Large histories use pages of 50 displayed rows.

## Replay a fast run

Open Sessions & Replay and choose Replay for a terminal session, or use the Replay
button on the selected session strip. The page is labeled **Recorded Session Replay**.

1. Restart returns to before the first retained event.
2. Next/Previous moves one real recorded event at a time.
3. Play/Pause advances the presentation; choose 0.5x, 1x, 2x or 4x speed.
4. Drag the position slider or use Seek in the event table to inspect a prefix.
5. In the unsafe run, advance from event 15 to 16 to reveal the actual C warning.

Replay time is illustrative; the original timestamps never change. A retained
history gap means replay cannot establish missing historical state. It will tell
you that explicitly. The program's actual terminal outcome remains visible even
when replay is paused or rewound.

## Controlled actual-deadlock demonstration

The catalog's **Controlled deadlock** intentionally blocks two workers on conflicting
requests. It is available only through the backend's approved bounded supervisor.
The backend terminates it after its fixed 2.5-second deadline and reaps the process.
The UI shows **Controlled expected timeout**, preserves state timed_out and reports
cleanup separately from the C warning. A killed process lacks normal session-end
metadata, so ingestion completeness is false; this is expected and clearly shown.

Use Stop session while it is running to terminate early. A browser disconnect does
not stop the process; the backend retains responsibility for the deadline. Reconnect
reloads retained evidence and displays a continuity notice rather than promising
that no events were missed.

## Sessions and reports

Sessions & Replay lists actual retained sessions with start/end, outcome, events,
warnings and completeness. Refresh it to discover sessions created elsewhere.
Inspect opens a session. Sessions live in backend memory and disappear on restart.

Reports includes actual stdout/stderr, session evidence, counts and limitations.
Export JSON downloads that evidence; Print uses a print-friendly layout. Integers
too large for JavaScript Number are exported as exact decimal strings, with this
encoding documented in the report. Reports contain no benchmark estimates.

If a page shows no selected session, start a program or inspect one from the session
list. If a program fails, inspect its error and captured stderr. 'Complete within
monitor scope' still excludes unsupported pthread operations; see EVENT_SCHEMA.md.
Checking consistent lock ordering is general investigation guidance, not an
automatically verified fix. The Performance section now shows saved measured C benchmarks with raw CSV downloads, separate from session reports. Expand Explain this demo for beginner guidance.
