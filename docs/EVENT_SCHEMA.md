# Runtime JSON Lines contract — schema version 1

Each record is one JSON object followed by LF, with no console prefix or ANSI
codes. Use a dedicated writable descriptor via `RDP_EVENT_FD`; stdout is reserved
for the application. All labels are internal constants and pass through JSON
escaping; environment values are never inserted as unescaped JSON. The current
maximum record buffer is 4096 bytes including its terminating C NUL; overflow
stops delivery rather than truncating a record.

## Common fields

| Field | Meaning |
| --- | --- |
| `schema_version` | Integer 1; consumers must reject unsupported major schema versions |
| `session_event` | Event name from the table below |
| `timestamp_ns` | Integer CLOCK_MONOTONIC time in nanoseconds, not wall-clock/Unix time |
| `process_id` | Actual Linux PID |
| `thread_id` | Actual Linux gettid TID of the emitting thread |
| `thread_instance` | Process-local tracker generation; 0 for events before that thread has a tracker |
| `sequence` | Process-local event delivery-attempt counter, starting at 1 |
| `monitoring_complete` | No detected loss/inconsistency within the supported observation model; false is sticky |

Never infer all pthread APIs are covered merely because `monitoring_complete`
is true. Condition waits, timedlock, trylock, and calls bypassing interposition
are unsupported. Read RUNTIME_MONITOR.md before interpreting coverage.

## Event-specific fields

| Field | Presence / meaning |
| --- | --- |
| `lock_id` | Known mapped mutex generation, formatted `lock_<hex counter>`; IDs are strings, not raw addresses |
| `mutex_address` | Full address as a hexadecimal string, for audit; no dereference is used to assign identity |
| `lock_kind` | `normal`, `errorcheck`, `recursive`, or `unknown` when mapping is available |
| `return_code` | Only after a real pthread operation returns; exact result, zero means ordinary success |
| `monitor_status` | Nonzero internal status accompanying an error reason; not necessarily a pthread result |
| `reason` | Stable descriptive label for incomplete/configuration/unknown-kind/sink conditions |
| `held_locks` | Tracker snapshot of distinct lock IDs; before request or after operation outcome |
| `held_depths` | Positive depths parallel to held_locks |
| `held_lock`, `requested_lock` | Warning's actual closing dependency endpoints |
| `closing_edge` | Two-element array `[held_lock, requested_lock]` |
| `cycle` | Full closed directed path, requested -> ... -> held -> requested |
| `graph_sequence` | Phase 3 new-edge sequence; separate from event-delivery sequence |

When mapping/tracker initialization fails, `lock_id`, kind, or held snapshots may
be absent. An incomplete held snapshot can be stale: it must not be displayed as
confirmed current ownership. Failed-operation records report attempts/outcomes,
not successful ownership changes. Request records do not contain return_code.

## Event types

| Event | Meaning |
| --- | --- |
| `session_start` | Monitor initialized; reason states explicit-init/static-unknown policy |
| `session_end` | Normal process shutdown reached monitor destructor; absent on SIGKILL/crash |
| `thread_start` | New tracker instance for a real runtime thread |
| `thread_end` | Tracker cleanup at thread exit; includes final snapshot |
| `mutex_init` | Real initialization returned zero; mapped generation and type recorded |
| `mutex_init_failed` | Real initialization failed; no new successful identity is claimed |
| `mutex_destroy` | Real destruction succeeded; known mapping retired |
| `mutex_destroy_failed` | Destruction failed; known mapping retained |
| `mutex_kind_unknown` | First observed use lacks observed initialization; conservative policy applies |
| `lock_request` | Actual intercepted request, emitted before analysis and the real lock call |
| `lock_acquired` | Real pthread_mutex_lock returned zero |
| `lock_failed` | Real lock returned an ordinary nonzero failure; held set is not enlarged |
| `lock_owner_dead` | Real lock returned EOWNERDEAD, which grants ownership with inconsistent robust state; unsupported analysis is incomplete |
| `lock_released` | Real pthread_mutex_unlock returned zero |
| `unlock_failed` | Real unlock failed; no held-lock removal |
| `potential_lock_order_cycle` | Phase 3 BFS found a path closed by a new observed request dependency |
| `monitoring_incomplete` | First transition caused by capacity, unsupported detected semantics, bad configuration, or analysis error |
| `sink_error` | Emergency stderr record: delivery failed, sequence identifies the failed delivery attempt |

Warnings contain graph evidence, not a declaration of current blocking. Repeated
identical closing edges do not generate repeated warnings within one process.

## Ordering, framing, and delivery

The output mutex assigns sequence/timestamp and writes a complete record before
the next monitor writer. Time is nondecreasing in stream order. Same-thread event
order follows that thread's hooks. Across threads this is observation/serialization
order, not an exact order of application mutex state transitions: another thread
may acquire a mutex after the real unlock but before its release record is written.
Do not treat the stream as an atomic global ownership snapshot.

Writes use one bounded syscall, within Linux PIPE_BUF, to avoid interleaving
monitor records in a dedicated pipe. A regular file still requires exclusive
ownership from unrelated writers. Short writes or write errors disable further
delivery and attempt one sink_error on stderr. A partial final line is possible
on sink failure or external process termination; consumers must flag incomplete
delivery, not invent a missing record. An emergency record retains the failed
attempt's sequence; it is not a successful retry of the original event.

Logging is synchronous: regular files/blocking pipes can delay threads. A pipe
reader must drain concurrently. Full nonblocking pipes fail explicitly instead
of changing mutex return values. There is no persistence guarantee, acknowledgement,
replay protocol, network service, or asynchronous queue in Phase 4.

One stream should belong to one child process. PID and sequence are not globally
unique across separate runs; Phase 5 assigns an external session identifier
when launching each child. Keep nanosecond timestamps/large counters losslessly
(Python int, or a suitable representation before JavaScript conversion).

## Phase 5 consumer contract

The implemented backend creates a dedicated pipe, inherit its write FD into the
child, set `RDP_EVENT_FD` and LD_PRELOAD only for that child, close its own unused
write end, and drain JSONL concurrently with separate stdout/stderr readers.
Validate schema, required types, ordering, and unexpected EOF; keep incomplete
and unknown-kind annotations visible. Correlate warnings by closing_edge/cycle
within the externally assigned session. The parent owns timeout, process-group
termination, and reaping. These requirements are implemented by backend/app/process_runner.py and the validated ingestion layer; see BACKEND_ARCHITECTURE.md.

Actual records are available after tests in `build/runtime-tests/safe.jsonl`,
`unsafe.jsonl`, and `deadlock.jsonl`. They contain real PIDs/TIDs/timestamps from
the last run and should not be treated as stable fixtures or fabricated examples.
