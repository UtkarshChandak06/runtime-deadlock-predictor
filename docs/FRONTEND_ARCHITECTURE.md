# Phase 6 frontend architecture

The frontend is a local React/TypeScript application. Vite builds it and Tailwind
integrates with a custom responsive dark theme. React Flow renders actual lock
dependencies; Lucide supplies icons. Recharts was not added because this phase
does not have a meaningful measured performance series. Hash navigation provides
shareable section/session/replay URLs without a routing dependency.

## Responsibilities and state

App owns the catalog, session list, selected route, start/stop actions and shell.
Launcher and Sessions are pages. LockGraph, Threads, Timeline, Alerts, Replay and
Report are reusable investigation components. No single component implements all
of these views. Shared panels/notices consistently expose loading, missing data,
failures, truncated history and incomplete monitoring. Components are formatted
for maintenance; production code never imports the explicit test fixtures.

The typed API layer centralizes routes, errors, abort signals and the Vite base URL.
It reads the real catalog, creates sessions with auto_start:false, and fetches the
actual session/graph/thread/alert/history resources. A synchronous launch guard and
disabled pending button prevent duplicate in-flight creates. Missing sessions
explain that backend restarts clear retained memory. HTTP failures remain visible.

Every HTTP/WS payload uses lossless-json before converting numeric values. Safe
integers become Number; integers outside JavaScript's safe range remain exact
decimal strings. Timestamps and identities are not rounded. Reports explicitly
document this serialization boundary. Monitor timestamps remain CLOCK_MONOTONIC
nanoseconds; duration uses backend UTC started_at/ended_at only.

## Live socket contract

useSession owns one socket for the selected session. It first fetches an HTTP
baseline, then connects to /ws/v1/sessions/{id}. For a newly created session it
calls start only after receiving the server's atomic session_snapshot. Tiny banking
programs can therefore emit real events to an already-attached browser without
changing their timing or adding synthetic records.

The stream reducer uses the outer envelope cursor for ordering and accepted-event
index for deduplication. Snapshots replace retained history; updates at an already
applied cursor are ignored. Nonconsecutive cursors and reconnects retain a visible
continuity-loss notice. runtime_event appends real records, session_status updates
the actual outcome, and monitoring_error retains up to 20 server error codes.
The graph/thread/alert views are refreshed from their authoritative REST resources
after a short debounce. Results are discarded/retried if the stream cursor changed
during that request. They are observations sampled from endpoints, not one atomic
global scheduler snapshot. Terminal WS closure is normal and labeled Recorded session.

An interrupted active stream retries at 0.5, 1, 2 and 4 seconds. Each attempt
reloads an HTTP baseline and then receives a fresh atomic WS snapshot. After four
attempts it exposes a disconnected error and a manual retry. There is no durable
replay guarantee across disconnects. Selecting another session/unmounting aborts
reads, clears timers and closes the old socket; stale session messages are ignored.
Connection code has dedicated fixture tests, plus real Chrome reconnect tests.

Event history is paginated with after/limit and bounded to 10,000 records, matching
the backend maximum. Timeline renders at most 50 rows per page and filters by type,
thread and lock ID. Retention gaps are explicit. Graph/threads/alerts retain the
backend capacity flags. Zero observed warnings is not a universal safety guarantee.

## Graph and thread views

Live graph nodes and edges are taken from the backend graph projection. Small
graphs use spaced rows; larger graphs continue on a nonoverlapping grid. Nodes
can be dragged; React Flow provides pan, zoom, fit and minimap. The grid is intended
for bounded teaching workloads, not a large-graph layout optimization benchmark.

Edges have arrowheads and selection evidence. Opposite dependencies attach to the
facing sides of their two nodes with opposite curves, so both arrowheads remain
visible. Cycle highlighting is membership in consecutive pairs from actual C
warning cycle arrays; it is not cycle detection. No edge is added merely to draw
a warning. Unknown or truncated graph state is labeled incomplete. Selected nodes
show actual generation/kind/address metadata rather than invented source locations.

Threads use (thread_id, thread_instance), observed operation counts, last event,
last observed held locks/request and recent actual history. They never claim a
request proves blocking or deadlock. A potential cycle is always distinct from
the controlled fixture's expected timeout and separately reported cleanup evidence.

## Recorded Session Replay

Replay is available for terminal sessions with retained history. It iterates the
actual accepted-event sequence prefix. Play, pause, restart, previous/next, range
seek and 0.5/1/2/4x speed alter presentation only. One event takes 800ms at 1x; this
is explicitly not historical execution duration. Original timestamps remain visible.

The replay projector derives identities/snapshots from present fields and historical
held-at-request dependencies from reliable lock_request records. Known recursive
reentry is skipped. It has no path search or independent warning logic: only a
potential_lock_order_cycle event reveals a warning. Before event 16 in the real
unsafe run the warning is absent; at that event its actual cycle is highlighted.
Seeking rebuilds from the prefix, so later evidence cannot leak backward.

Truncated histories or sequence gaps make reconstructed state limited/unknown;
the projector does not invent prior edges. Thread snapshots without sufficient
evidence are marked unreliable. Replay projection storage is capped at 1,024 per
category. A C capacity failure, stream loss, retained-history loss and replay
capacity are distinct limits. Replay cannot reconstruct kernel scheduling state,
unobserved APIs, source code locations or missing records. Session terminal outcome
is displayed separately from playback state throughout.

## Reports, accessibility and scope

Reports include actual session outcome, counts, projections, warnings, completeness,
cleanup and captured stdout/stderr. Export downloads the retained snapshot as JSON;
print rules provide a readable report. There are no fabricated performance charts.

Navigation and controls are keyboard-accessible with focus indicators and labels;
warnings/statuses use text as well as color. Narrow layouts have a navigation toggle,
wrapping cards and locally scrollable tables. Reduced-motion and print styles exist.
Chrome was tested at 1366x768, 1920x1080 and 390x844; no formal WCAG audit or
cross-browser compatibility certification is claimed.

All process launching remains in the existing bounded backend. No backend/C
contract change was necessary. The frontend has no executable uploads, command
execution, persistence, authentication, cloud hosting or automatic prevention.
Phase 7 adds saved benchmark measurements, beginner help, report provenance and continuity recovery; see FINAL_TEST_REPORT.md and PERFORMANCE_EVALUATION.md.
