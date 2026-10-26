# Phase 5 HTTP and WebSocket contract

Base URL: `http://127.0.0.1:8000`. `/docs` and `/openapi.json` expose exact Pydantic
response fields. All session IDs are generated UUIDs. The catalog accepts only
banking_safe, banking_unsafe and banking_deadlock; it reports build availability.

| Method | Route | Response |
| --- | --- | --- |
| GET | /api/v1/health | status, phase, active_sessions, retained_sessions |
| GET | /api/v1/applications | bounded catalog array with scenario and supervision metadata |
| POST | /api/v1/sessions | 201 SessionView |
| GET | /api/v1/sessions | retained SessionView array |
| GET | /api/v1/sessions/{id} | SessionView |
| POST | /api/v1/sessions/{id}/start | start a created session; SessionView |
| POST | /api/v1/sessions/{id}/stop | wait for cleanup; SessionView |
| GET | /api/v1/sessions/{id}/events?after=0&limit=200 | EventPage; limit 1..1000 |
| GET | /api/v1/sessions/{id}/graph | nodes, edges, reported_cycles, completeness |
| GET | /api/v1/sessions/{id}/threads | last-observed thread records and completeness |
| GET | /api/v1/sessions/{id}/alerts | original alerts, total_received, truncated |

Create body: `{"application_id":"banking_unsafe","auto_start":true}`.
Unknown fields are rejected. `auto_start` defaults true. For deterministic live
subscription to tiny workloads, create with false, attach WS, then POST start.
No artificial workload delays are introduced. Start/stop require no body.

SessionView includes identity/state, UTC timestamps, historical PID, exit_code,
event_count, warning_count, invalid_records, C/ingestion/combined completeness,
history_truncated/dropped_events, bounded stdout/stderr and truncation flags,
error, cursor, cleanup_verified, and optional controlled_deadlock_evidence.
Errors use `{"detail":"code"}`; body-validation 422 uses FastAPI's detail array.
404: unknown app/session; 409: invalid transition; 429: concurrency/retention cap;
503: unavailable approved build/shutdown; 403: local Host/Origin/peer violation;
413: oversized or stalled POST body. A valid create can later fail asynchronously;
read the terminal session status rather than assuming launch means completion.

EventPage shape:

```json
{"items":[{"index":1,"event":{"schema_version":1}}],"next_after":1,
 "oldest_available":1,"total_received":1,"gap":false,"history_truncated":false}
```

The event above is deliberately abbreviated to illustrate the envelope, not a
valid runtime fixture. Real events retain every original C field. `index` is the
accepted-record ordinal; `event.sequence` is the C delivery-attempt counter.
`gap` is true when the requested cursor precedes retained history. Set after to
next_after for the next page. Large nanosecond counters are JSON integers preserved
losslessly in Python; Phase 6 must use lossless JSON parsing/BigInt for integers
beyond JavaScript's safe range, rather than silently round them with Number.

## WebSockets

Connect to `ws://127.0.0.1:8000/ws/v1/sessions/{id}` with the documented local
Origin. Unknown sessions/origins are rejected before acceptance. Client-cap
rejection also occurs before acceptance. This is an output-only channel.

Every envelope has `type`, `session_id`, `cursor` and `data`:

- session_snapshot: session, retained event page, graph, threads, alerts and recent errors.
- runtime_event: `{index,event}` with the unmodified C event.
- session_status: SessionView after a lifecycle transition.
- monitoring_error: bounded error code, including slow_consumer recovery guidance.

The outer cursor increases for every publication (including statuses/errors),
independently of C sequence and accepted-event index. The snapshot's outer cursor
is the synchronization boundary. Subscription and snapshot copying run without
await on the same event loop, so queued updates begin strictly after that boundary.
Use the outer envelope cursor; an embedded status view is captured just before
the status publication increments the channel cursor.

Each client has its own bounded queue. Overflow discards its pending queue, sends
slow_consumer when possible, and closes with 1013; sends time out after two seconds.
No network acknowledgement/durable replay is promised. Reconnect for a new bounded
snapshot and inspect its gap/truncation flags. Client disconnect does not stop a
session. Terminal status closes normally; a late terminal subscriber gets one
complete retained snapshot then closes. Multiple clients receive independent copies
of the same observations. Restart loses sessions. Phase 6 consumes these routes
and envelopes directly; see FRONTEND_ARCHITECTURE.md.
