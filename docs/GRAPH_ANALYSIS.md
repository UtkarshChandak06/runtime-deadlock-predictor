# Phase 3: held locks and historical request-order analysis

## What is implemented

`build/libdeadlock_analysis.a` is a reusable C11 static library. Tests and the demo
supply explicit events to it. The static library itself does not intercept pthread
calls or prevent deadlocks. Phase 4 now supplies its events through a separate
preload shared library; see RUNTIME_MONITOR.md. Phases 5–7 remain unimplemented.

The held-lock tracker answers: **what locks has this logical thread successfully
acquired and not yet successfully released?** It holds one entry per mutex with
a recursion depth. A request only sets pending metadata. A successful acquisition
adds an entry or increases a valid recursive entry's depth. A failed acquisition
clears pending metadata without adding a held lock. A successful release reduces
depth, removing the entry at zero; a failed release changes nothing.

The graph answers a different question: **what nested requests have we seen over
this session?** Locks are nodes, identified by opaque, nonzero `uint64_t` IDs.
An arrow A -> B means a caller reported holding A when requesting B. The direction
matters: sharing an undirected connection does not establish a directed cycle.
Edges remain after unlock and after failed acquisitions. This is historical
request evidence, not a snapshot of current ownership or waiting.

## How BFS finds and reconstructs a cycle

Before inserting H -> L, the graph searches from L toward H using breadth-first
search. A queue visits reachable nodes in increasing path length. Each node is
marked when enqueued, so it is visited at most once, even in an already cyclic
graph. A parent index records which node discovered it.

If H is reached, walking parents backward from H reconstructs the existing path.
Reversing that path and appending L yields `L -> ... -> H -> L`. For existing
10 -> 20 and 20 -> 30, a new 30 -> 10 produces `10 -> 20 -> 30 -> 10`.
The diagnostic separately identifies 30 -> 10 as the actual closing dependency.
A normal self-request produces the two-entry path `L -> L`.

The adjacency matrix bounds a BFS to O(V²) time and O(V) temporary storage, with
V <= 32. The parent chain can include all 32 nodes; the closed path has at most
33 entries. Tests cover that maximum and a choice between shorter/longer routes.
One shortest witness is reported for each newly inserted closing edge, not every
possible cycle. An existing identical edge returns `RDP_DUPLICATE` and no warning.
Reset clears edges, nodes, sequence numbers, completeness failures, and therefore
warning suppression for a new session.

## Public API and modules

| File | Responsibility |
| --- | --- |
| `monitor/include/rdp/types.h` | IDs, limits, statuses, lock kinds, structured warning |
| `monitor/include/rdp/tracker.h`, `monitor/src/tracker.c` | Pending requests, held entries, depths, snapshots |
| `monitor/include/rdp/graph.h`, `monitor/src/graph.c` | Shared graph, BFS, insertion, queries, reset |
| `monitor/include/rdp/analyzer.h`, `monitor/src/analyzer.c` | Connect a request's held snapshot to graph analysis |
| `examples/analysis/graph_demo.c` | Explicit-event demonstration and formatting outside locks |

Initialize zeroed objects before use (never read truly uninitialized C memory):

```c
RdpGraph graph = {0};
RdpTracker thread = {0};
RdpStatus gs = rdp_graph_init(&graph, RDP_MAX_NODES, RDP_MAX_EDGES, RDP_MAX_PATH);
RdpStatus ts = rdp_tracker_init(&thread, 101, RDP_MAX_HELD, UINT32_MAX);
/* Check both statuses before continuing. */
```

The main event API is:

```c
RdpStatus rdp_analyze_lock_request(RdpGraph *, RdpTracker *, RdpLockId,
                                  RdpLockKind, RdpAnalysis *);
RdpStatus rdp_record_lock_acquired(RdpTracker *, RdpLockId, int pthread_result);
RdpStatus rdp_record_lock_released(RdpTracker *, RdpLockId, int pthread_result);
```

Supply the request before the application operation; then supply its actual
result. `pthread_result == 0` means success; positive error codes mean failure.
The supplied-event demo explicitly provides hypothetical operation outcomes to
exercise the algorithm; it does not pretend to execute those application locks.
Phase 4's runtime wrappers now obtain real supported pthread outcomes automatically.

Each tracker represents one caller-provided nonzero logical thread ID. Only one
request may be pending. A second request returns `RDP_BUSY`; a mismatched result
returns `RDP_STATE` without clearing the original pending request. Every accepted
request must eventually receive its matching result unless that thread remains
blocked. For a capacity error after request acceptance, still record the real
result. `rdp_tracker_snapshot` exposes the pending ID if recovery is needed.

Other operations:

- Tracker: `rdp_tracker_request`, `rdp_tracker_acquired`, `rdp_tracker_released`,
  `rdp_tracker_snapshot`, `rdp_tracker_init`, `rdp_tracker_destroy`.
- Graph: `rdp_graph_register`, `rdp_graph_add`, `rdp_graph_has_edge`,
  `rdp_graph_stats`, `rdp_graph_reset`, `rdp_graph_init`, `rdp_graph_destroy`.

Do not modify public struct storage directly or copy a live mutex-containing
object. Initialization/destruction require no concurrent users. A logical session
reset should also be done after stopping event producers; reset trackers by
destroying/reinitializing them only when their old state is no longer needed.

## Structured warnings

`RdpAnalysis` contains `warning_count`, up to 16 `RdpWarning` records, and
`complete`. A warning contains:

- `type = RDP_POTENTIAL_LOCK_ORDER_CYCLE` (formatted as `potential_lock_order_cycle`).
- Caller-provided `thread`, `held_lock`, and `requested_lock` IDs.
- Closing dependency `held_lock -> requested_lock`.
- Full `cycle[]` and `cycle_length`, including the repeated endpoint.
- `sequence`, a graph-session counter incremented for each new edge.

These are copied values, suitable for later serialization. There is no JSON
transport, callback, socket, or event-stream implementation. A no-warning result
does not prove safety; always inspect both status and completeness.

## Recursion, self-requests, and false positives

Use `RDP_RECURSIVE` only when the caller knows the mutex supports recursive
locking. Reacquiring an already-owned recursive mutex cannot block on another
owner, so the analyzer skips all dependencies for that reentry, including edges
from other held locks. Its depth increases only on successful acquisition.

`RDP_NORMAL` self-requests can warn about self-deadlock risk. `RDP_UNKNOWN` is
conservative and follows the same warning policy. Unknown recursive mutexes may
therefore produce false positives. A reported successful non-recursive reentry
is inconsistent with this model and marks tracking incomplete. Kind must remain
consistent while a lock is held; the library does not intercept initialization
or maintain a process-wide mutex-kind registry. Callers must supply consistent
metadata across trackers.

An edge remains even if a request fails (for example, a future trylock returns
EBUSY). Historical events from different times can close a cycle without any
current blocking. External serialization can also make conflicting orders benign.
Thus a graph cycle means **potential deadlock**, not proof of an actual deadlock.
The library does not claim to detect all deadlocks, including non-mutex resource
waits. Reused mutex addresses require caller-managed generations/new identities
or a session reset; otherwise unrelated lock lifetimes could be merged.

## Concurrency and resource limits

Each tracker protects its state with one internal pthread mutex. The graph has
one mutex covering searches, duplicate checks, insertions, queries, and reset.
The analyzer takes a tracker snapshot and releases its mutex before accessing
the graph: internal mutexes are never nested. No application mutex operation,
callback, or output is performed under either internal mutex. The whole request
batch is not a transaction: its individual edges serialize with other callers.
Same-tracker calls are data-race safe, but must respect logical event order; a
thread must not submit an acquisition result before its analysis request returns.

| Resource | Compiled maximum | Smaller runtime limit supported |
| --- | --- | --- |
| Graph nodes | 32 | Yes |
| Distinct directed edges | 128 | Yes |
| Distinct held locks per tracker | 16 | Yes |
| Recursive depth | UINT32_MAX | Yes |
| Closed cycle path entries | 33 | Yes, minimum 2 |

The implementation uses fixed storage, with no dynamic allocations and therefore
no allocation-failure path. Queue indices and parent paths stay within node bounds.
`RDP_NODE_LIMIT`, `RDP_EDGE_LIMIT`, `RDP_PATH_LIMIT`, and `RDP_SEQUENCE_LIMIT`
make the graph's completeness flag sticky-false and stop further mutations until
reset. An individual failed edge insertion adds no nodes/edges and emits no
truncated warning. A multi-edge request may already have committed earlier edges
or warnings; they remain valid evidence but `complete=false` reports partial
coverage. Tests explicitly check this case.

`RDP_HELD_LIMIT`, `RDP_DEPTH_LIMIT`, or an inconsistent successful acquisition
make that tracker's completeness sticky-false. A snapshot reports
`RDP_INCOMPLETE`; later request analysis cannot claim clean coverage. Destroy and
reinitialize only at a safe session boundary. Graph completeness describes graph
storage alone; it cannot recover missing events from an incomplete tracker.

Other statuses include `RDP_INVALID`, `RDP_UNINITIALIZED`, `RDP_NOT_FOUND` for
unknown releases, `RDP_STATE` for inconsistent event order/kind, `RDP_BUSY`, and
`RDP_SYNC_ERROR`. Synchronization errors must be treated as fatal to the affected
analysis session; there is no automatic recovery from a damaged internal mutex.

## Banking compatibility and verification

The banking programs have not been instrumented or changed. Separate tests supply
their conceptual safe order 1 -> 2 twice (no cycle), then the reverse order 2 -> 1
(a two-node cycle). This tests analysis semantics, not live banking observation.

```bash
make analysis-demo
make check-phase3
make check-phase3-sanitize  # AddressSanitizer + UndefinedBehaviorSanitizer
make check-phase3-tsan      # ThreadSanitizer, where supported
```

The normal suite tests graph/tracker/analyzer values and runs the concurrent test
20 times. Each concurrent run uses 8 workers with 100 event sequences each, while
main reads snapshots; only one identical closing edge warns. Every test process
has a timeout. Actual outcomes are recorded in VERIFICATION.md.

Phase 4 connects real request/acquisition/release outcomes to these unchanged APIs
and provides thread-local tracker ownership, mutex identity/lifetime management,
and reentrancy protection. Its runtime hooks have a narrower supported-operation
scope than all of pthreads; see RUNTIME_MONITOR.md for exact boundaries.
