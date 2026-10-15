# Architecture through combined Phases 4 and 5

Implemented modules: `hooks.c`, `tracker.c`, `graph.c`, `report.c`, `log.c`.
Only `pthread_mutex_lock` and `pthread_mutex_unlock` are intercepted. Graph
storage and tracker behavior from Phases 1-3 are retained.

## Acquisition flow

1. Save application errno, enter the existing TLS reentrancy guard, and resolve
   the real functions with `pthread_once` / `dlsym(RTLD_NEXT)`.
2. Snapshot the calling thread's held-lock collection (capacity 64).
3. For each H, analyze and record H -> requested L under the graph mutex.
4. After graph unlock, format and submit a complete cycle report if detected.
5. Restore errno and call the original pthread function with no monitor lock.
6. Add L to the tracker only on return code zero. Successful unlock removes
   an entry; failed lock/unlock leaves the held state unchanged.

Pre-acquisition analysis records intent even when the original call never
returns. Failed attempts remain historical graph edges, not held locks. Normal
lock/unlock and library-startup traces are disabled. No debug setting or other
synchronization wrapper was added.

## Graph representation and traversal

Opaque mutex-address tokens are compared for equality, never dereferenced.
An append-only node array supplies stable indexes into a byte adjacency matrix.
Each cell distinguishes absent, present, and reported closing edges.
BFS follows L -> ... -> H before insertion of H -> L, under the same mutex.
Parent indexes reconstruct the entire path. The API returns that path excluding
the repeated L; reporting appends L. Self-edges are cycles too, because mutex
attributes are unknown. All output pointers are optional.

One consistent snapshot is used per dependency. Dependencies from a held set
are individually atomic; the edge that closes a concurrently constructed cycle
sees the preceding edges. Reads and resets use the same graph mutex. BFS is
O(V^2), since neighbors are found by scanning matrix rows; lookup is O(V) and
scratch is O(V). Both update APIs enforce 1024 nodes / 4096 edges. Static
storage is about 1.008 MiB on 64-bit Linux; there is no graph allocator to fail.
A rejected dependency returns ANALYSIS_INCOMPLETE without partial insertion,
and increments a saturating dropped counter. Loss remains historical even if
later calls succeed. Init preserves history; cleanup is for isolated tests.

Deduplication remains bounded in the graph matrix: one report attempt per
closing directed edge. Repeated closing edges are suppressed; newly added
cycle-closing edges still report, including cycles involving previously reported
edges. This is not enumeration or canonical deduplication of all possible
cycles. A graph cycle is not proof that any feasible interleaving hangs.

## Warning records

`rdp_report_cycle` accepts the Linux kernel TID, held and requested identities,
and complete path. It prints the heading explicitly saying potential and not
confirmed, followed by Thread, Held Lock, Requested Lock, and Cycle fields.
The cycle starts at requested L and ends at L after the closing H -> L edge.
No fabricated filenames, line numbers, thread names or mutex labels are used.

A fixed stack buffer has room for 1025 pointer representations and separators
plus metadata: `512 + (1024 + 1) * (2 * sizeof(void *) + 8)` bytes (25,112 on
64-bit Linux). Every formatting result is checked. Invalid path/format overflow
produces an explicit incomplete-diagnostics message rather than a truncated
cycle. Small diagnostics use a separate 512-byte buffer with explicit overflow
reporting. Actual maximum-path output is tested, not just buffer arithmetic.

## Output without holding monitor locks

The previous multi-write per-edge logger could mix simultaneous warnings.
The updated logger uses a statically allocated 16-record FIFO and a short
queue mutex, separate from the graph mutex. The graph is always unlocked before
record submission; the two mutexes are never nested by the monitor.

A producer copies its bounded record under the queue mutex. If no writer is
active, it elects itself as writer. Other producers enqueue and return; they
never wait for writer ownership or output completion. The elected writer copies
one queued record into its local buffer, unlocks the queue mutex, and completes
all writes for that record before fetching another. The writer flag is not a
spinlock: producers never wait on it. No internal mutex is held across writes,
formatting, or the original application lock call. Short writes and EINTR are
handled while preserving contiguous monitor records. The queue occupies about
393 KiB; logging uses no heap allocation or background worker.

The wrapper guard also surrounds logger mutex operations, so monitoring does
not observe its own synchronization. A logger TLS recursion flag prevents nested
record emission. Both submission and error-counter queries preserve errno.
Queue overflow increments a saturating lost counter and pending-loss summary;
when output progresses the writer emits an explicit diagnostics-INCOMPLETE
record. It cannot promise to deliver this summary to a broken sink.

Zero-byte writes and errors stop that record and increment an error counter,
avoiding an infinite retry loop. SIGPIPE is blocked only on the writer thread,
and a newly generated broken-pipe signal is consumed before restoring the
prior signal mask; a preexisting pending signal is preserved. No process-wide
signal disposition is changed. Query symbols `rdp_log_errors()` and
`rdp_log_dropped()` expose failure counts. Delivery errors do not roll back or
corrupt graph/tracker data. Report-attempt deduplication is not delivery-aware.

Deferred cancellation is disabled only during queue critical sections. Writer
cleanup releases election ownership if cancellation occurs during output; queued
records may await the next producer. Asynchronous cancellation is unsupported.
A stalled sink may block the elected caller (which may hold application locks);
other producers continue with bounded enqueue/drop behavior. There is no promise
of nonblocking diagnostics. Cancellation, immediate process exit, `_exit`, or
signals can lose in-flight records. No drain-at-exit guarantee is made when the
application exits while another thread is reporting. Unrelated application or
other-process stderr writes are outside the monitor's serialization guarantee.

## Limits and interpretation

Tracker capacity remains 64 distinct held locks per thread with recursion depth;
overflow warns once per thread and counts lost acquisitions. Missing held locks
can hide dependencies. Graph limits likewise can hide cycles. Node identities
are addresses, not permanent object IDs: destroy/reinit or address reuse aliases
history. Single locks without dependencies need not appear as graph nodes.

Historical cycles can be harmless because operations run sequentially, gates
serialize them, or attempts fail. The sequential two/three-lock fixtures finish
successfully while warning, demonstrating this distinction deterministically.
Recursive re-locks and error-checking self-attempts can also warn. Only return
code zero updates the tracker; robust owner-death acquisition is unsupported.

Trylock, timedlock, condition waits, mutex lifecycle, other synchronization
primitives, static executables, cross-process locks, signal-handler use, fork
with active monitor state, and unloading an active monitor remain unsupported.
Condition waits can leave the held set stale. No benchmark or GUI was added.

## Verification

Existing tests remain intact. `make run` / `make run-bare` run every regression
with 15-second timeouts. `make check-diagnostics` asserts real captured warning
fields, exact full paths (including 1024 nodes), duplicate behavior, new cycles,
quiet normal programs, concurrent reports, failure handling and capacity warnings.
It also executes the intentional deadlock only as a separately captured process
with a two-second timeout, after which Python kills and reaps it.

The fault-injection logger unit test covers EINTR, short writes, EIO, zero writes,
queue overflow with a blocked writer, and eight simultaneous maximum-length
records. `make check-sanitizers` runs all four standalone unit executables under
ThreadSanitizer and AddressSanitizer/UBSan, with 30-second timeouts. These are
standalone checks, not sanitizer instrumentation of the preload integration.
See PHASE4_5_VERIFICATION.md for actual commands, outputs and remaining limits.
