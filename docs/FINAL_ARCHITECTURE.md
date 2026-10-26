# Final architecture

```text
Real C pthread workload -> LD_PRELOAD hooks
  -> TLS held-lock tracker + synchronized historical graph + BFS
  -> JSONL on inherited RDP_EVENT_FD
  -> FastAPI supervisor / validated ingestion / bounded projections
  -> REST snapshot + WebSocket envelopes
  -> React graph / alerts / threads / timeline / replay / report

Separate real C benchmark -> raw.csv + environment.json -> summary.csv
  -> verified static export -> Performance view (saved, not live)
```

Only C decides whether a request closes a historical cycle. Requesting B while holding A adds A -> B before the real blocking call. Successful acquisition/release changes held state. Failed requests may leave conservative historical edges. Explicit recursive depth avoids spurious self-dependency; unknown static kinds remain visible and conservative. BFS reconstructs a concrete cycle path. Repeated closing-edge warnings are suppressed and reused addresses receive distinct identities.

Internal synchronization is released before blocking on an application lock. Interception guards prevent recursion into monitor operations. JSON logging is synchronous and serialized, so it can perturb scheduling and impose large overhead.

The backend launches fixed catalog entries with fixed arguments, a private environment, dedicated pipes and an owned process group. It drains monitor/stdout/stderr independently. Stop, timeout, cancellation and shutdown terminate and reap owned children. C completeness, ingestion completeness and retained-history truncation are separate. Missing normal endings after a kill cannot be reported as a fully complete stream.

Default bounds are two concurrent sessions, twenty retained sessions, 2000 retained events/session, 64 KiB per captured output stream, 4096-byte input records, 128-envelope subscriber queues and eight WebSocket clients/session. The C graph has 32 nodes/128 edges, held sets have 16 distinct locks and the registry has 64 identities. Bounds trigger explicit incomplete/truncated evidence.

React receives a baseline plus an atomic WS snapshot, preserves large integers, retries connections with bounded delays and retains continuity warnings. Terminal closure reloads retained history. Replay projects recorded prefixes without launching a process or independently detecting cycles; missing records invalidate reliable reconstruction. Reports include actual catalog metadata, events, outcome, completeness, cleanup and client continuity. Saved benchmarks have independent provenance.

The service is loopback-only, single-worker and memory-only, with Host/Origin checks and no authentication. Sessions disappear on restart. Production bundling is verified; production deployment is outside scope. See [API contract](API_CONTRACT.md), [runtime semantics](RUNTIME_MONITOR.md) and [final limitations](FINAL_LIMITATIONS.md).
