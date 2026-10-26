# Phase 5 backend architecture

The existing C engine remains authoritative. FastAPI launches only three fixed
repository-relative banking executables, reads their dedicated JSONL descriptors,
and projects their observations. Python contains no cycle-detection algorithm.
No C sources or schema were changed for this phase.

## Child and descriptor ownership

`process_runner.supervise` verifies trusted executable/library paths, copies the
monitor to a private `/tmp/deadlock-sentinel-*/monitor.so`, compares SHA256 hashes,
and supplies that absolute whitespace-free path in the child's `LD_PRELOAD`.
Linux's loader splits preload values at spaces even when a shell value is quoted.
The copy is deleted after cleanup. This verifies copy integrity, not provenance
of a locally rebuilt library; repository artifacts are trusted inputs.

An `os.pipe()` write FD is inherited using `pass_fds`; its actual number is passed
as `RDP_EVENT_FD`. The parent closes its copy immediately after spawn. The child
gets only a small explicit environment and no shell. `start_new_session=True`
creates a process group. Three asyncio readers concurrently drain monitor output,
stdout and stderr. StreamReader flow control bounds unread transport buffering;
retention limits do not stop draining. Network subscribers never block ingestion.

All applications have deadlines. The controlled fixture receives
`--controlled-fixture` only inside the supervisor, with a fixed 2.5-second limit.
It captures `/proc` wait channels before SIGKILL, kills the group and reaps the
leader. Stop, errors, spawn cancellation, and application shutdown also clean up.
Each result reports whether the PID and group are absent. No arbitrary processes
or commands are accepted. The approved programs do not fork descendants.

## Lifecycle and evidence

`created -> starting -> running -> completed | failed | timed_out | stopped`.
Created sessions can also be stopped. Invalid transitions return 409. Failed spawn
and a zero-event stream are failures. A nonzero exit is failure unless the
supervisor stopped/timed out the process. Completed means real zero exit with
monitor events; inspect completeness separately. UTC session timestamps differ
from original monotonic nanoseconds. PID remains as historical identity after exit.

Parsing validates bounded LF-delimited records, strict types, operation results,
snapshots, warning paths, PID, sequence and monotonic ordering. Original fields are
retained without guessed defaults. Unsupported version/types, invalid UTF-8/JSON,
duplicate keys, malformed shapes and partial EOF count as invalid records.
Oversize lines are discarded through LF without unbounded accumulation. Invalid
records are excluded from projections and history; bounded error codes are exposed.
All event types in EVENT_SCHEMA.md are accepted subject to their documented shape.
An emergency sink_error written on stderr stays in stderr; it is not mixed into
the dedicated event stream. Missing shutdown metadata marks incomplete delivery.

Four distinct flags preserve evidence boundaries: `c_monitor_complete` is sticky
C-reported coverage; `ingestion_complete` tracks parsing/order/lifecycle loss;
`history_truncated` means bounded retention dropped earlier accepted events;
`projection_complete` means projection capacity was not exceeded.
`monitoring_complete` combines C and ingestion flags (null before any evidence).
It does not promise coverage of unsupported pthread APIs. A killed fixture lacks
session_end, so ingestion completeness is false even when all prior records arrived.

Edges derive from each reliable lock_request's held_locks -> lock_id relationships.
Known recursive reentry skips all dependency additions, matching C semantics.
Cycle paths and alerts are exclusively original C warnings. Threads use actual
(thread_id, thread_instance), last observed snapshots and bounded operation history.
These are not atomic ownership/scheduler snapshots. Incomplete observations are
marked unreliable. Historical cycle warnings never label threads deadlocked.
The controlled fixture's separate evidence combines its two conflicting requests,
four stdout markers, C warning and three futex waiters; this is not a general detector.

## Limits and local boundary

| Setting (environment variable) | Default | Allowed range |
| --- | ---: | --- |
| DS_MAX_CONCURRENT | 2 | 1..8 |
| DS_MAX_SESSIONS | 20 | 1..100 |
| DS_MAX_EVENTS | 2000 | 1..10000 per session |
| DS_MAX_OUTPUT_BYTES | 65536 | 64..1048576 per output stream |
| DS_MAX_LINE_BYTES | 4096 | 256..16384 |
| DS_WS_QUEUE | 128 | 1..1024 per client |
| DS_MAX_WS_CLIENTS | 8 | 1..32 per session |
| DS_PROJECTION_LIMIT | 256 | 2..1024 nodes, edges, threads and alerts separately |
| DS_NORMAL_TIMEOUT | 5 seconds | 0.1..30 |

Errors retain 20 codes; each thread retains 20 operations. POST bodies are limited
to 1024 bytes with a three-second receive deadline. Retained sessions are never
silently evicted: the cap returns 429; restarting clears all memory. Output retains
a prefix and truncation flags. Event pages report retention gaps. Alerts retain the
latest bounded C warnings and expose their total count/truncation.

The supplied command binds 127.0.0.1, disables proxy-header trust, uses one worker,
and limits graceful shutdown to five seconds (including idle WebSockets).
Middleware independently requires a loopback peer and a local Host. HTTP and WS
Origin must be absent for local CLI clients or exactly one of:
`http://127.0.0.1:5173`, `http://localhost:5173`,
`http://127.0.0.1:8000`, `http://localhost:8000`.
CORS allows only these origins, GET/POST and Content-Type. There is no remote
authentication, persistence, remote monitoring or production hosting support.
Local processes remain trusted; this is not protection against hostile local users.
