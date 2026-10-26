# Phase 4: real runtime mutex monitoring

## What runs now

`build/libdeadlock_monitor.so` automatically observes supported pthread mutex
operations in dynamically linked Linux applications. The existing banking
sources are unchanged. The shared library compiles the tested Phase 3 engine
again with position-independent code; it does not reuse non-PIC archive objects.

`LD_PRELOAD` asks the dynamic loader to load a library before the ordinary
dependencies, allowing these exported functions to be selected first:

- `pthread_mutex_lock`
- `pthread_mutex_unlock`
- `pthread_mutex_init`
- `pthread_mutex_destroy`

The hooks resolve the next implementation using `dlsym(RTLD_NEXT, ...)` and call
it once with the original arguments. `RTLD_NEXT` searches after this library,
allowing the wrapper to forward rather than call itself. See the Linux manuals
for [the loader](https://man7.org/linux/man-pages/man8/ld.so.8.html) and
[dlsym](https://man7.org/linux/man-pages/man3/dlsym.3.html).

## Modules and initialization

`hooks.c` owns symbol resolution, the four wrappers, constructor/destructor, and
cancellation/errno preservation. Resolution uses pthread_once and a thread-local
resolving flag. Reentrant calls forward only if their original symbol is already
available. Missing symbols or an unforwardable bootstrap recursion produce a raw
stderr diagnostic and exit 127: inventing pthread success would be incorrect.
This bootstrap path is explicitly bounded, not a retry loop. The normal Linux
glibc resolution path was exercised by every integration run; forced loader
failure and other libc implementations were not tested.

`runtime.c` owns the process graph, thread-local trackers, the address/generation
registry, type metadata, and incomplete-state policy. `events.c` owns descriptor
validation, record formatting, output serialization, and SIGPIPE handling.
`runtime.h` and `events.h` describe internal module interfaces; the Phase 3 public
API and implementations were preserved.

## Reentrancy and synchronization

A thread-local guard is active during monitor work. If the graph, tracker,
registry, logger, or libc formatting calls an intercepted mutex function, the hook
forwards directly without generating application events. Tests verify that only
the two actual account mutexes appear in banking records.

The registry lock is released before analysis or output. The Phase 3 analyzer
releases its tracker snapshot lock before entering graph synchronization. The
logger takes its separate output mutex only after those locks are released. No
monitor mutex remains held across a blocking application mutex call. Formatting
uses a bounded stack buffer, outside the graph mutex; libc internals are protected
from recursive monitoring by the same guard.

Before a real lock call, the hook emits a request and feeds its current held set
to the Phase 3 analyzer. Any full-cycle warning is written before the real lock
can block. Afterwards, only actual return-zero success adds ownership. Unlock
tracking changes only after a real successful unlock. Results and application
errno are preserved. Error-checking mutex tests verify EDEADLK and EPERM without
inventing acquisitions/releases. Robust EOWNERDEAD is special: ownership can be
granted despite a nonzero result, so it is reported as `lock_owner_dead` and marks
the unsupported analysis incomplete, not as an ordinary failed acquisition.
See [POSIX mutex semantics](https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3p.html).

Monitor work temporarily disables cancellation and restores the previous state.
The event writer uses a raw write syscall, avoiding an added libc write cancellation
point. Deferred cancellation is tested with a pending request followed by the
application's own pthread_testcancel. Asynchronous cancellation and use from
signal handlers are unsupported. See [pthreads cancellation rules](https://man7.org/linux/man-pages/man7/pthreads.7.html).

## Threads and lock identities

Each real thread gets a zeroed C thread-local `RdpTracker`. The tracker uses the
Linux gettid identity. Records additionally include a process-local monotonic
`thread_instance`, so later OS TID reuse does not merge distinct tracker lifetimes.
A pthread-key destructor emits thread_end and destroys that thread's internal
tracker mutex. Sequential and concurrent thread lifetimes are tested. Returning
from main ends the session; it need not produce a main-thread key-destructor event.
Exit with held/pending tracker state marks monitoring incomplete.

The registry compares full `uintptr_t` addresses without dereferencing mutex
internals. It assigns IDs `lock_1`, `lock_2`, etc. from a non-reused generation
counter. Successful observed destruction retires a mapping. Reinitialization at
the same address receives a new ID; historical graph nodes remain for old IDs.
If destruction is not observed, address reuse can still merge lifetimes. There
are 64 live registry slots; the Phase 3 graph still allows 32 historical nodes,
128 edges, 16 held locks per thread, and a maximum 33-entry cycle path.

Initialization wrappers read types through public pthread_mutexattr_get* APIs,
never undocumented pthread fields. Explicitly initialized ordinary,
error-checking, and recursive private non-robust mutexes are supported. Recursive
depth is preserved and valid reacquisition creates no false self-cycle.

Static/pre-existing mutexes have unknown type. Their first use emits
`mutex_kind_unknown`. Ordinary unknown mutexes can be tracked, but self-requests
are treated conservatively. A static recursive mutex can therefore warn and mark
tracking incomplete after successful reentry; this limitation is tested, not
hidden. Kind metadata is exact only when initialization was observed.

## Supported scope and boundaries

| Case | Behavior |
| --- | --- |
| Ordinary lock/unlock | Monitored automatically for dynamically linked calls reaching these hooks |
| Recursive/error-checking | Supported when initialization is observed; exact return codes retained |
| Init/destroy | Successful init records public type; successful destroy retires generation |
| trylock/timedlock | Not intercepted; ownership changes may be missed; an unmatched successful unlock marks incomplete |
| Condition-variable waits | Not intercepted; release/reacquisition inside waits is not modeled; outside supported analysis scope |
| Robust recovery | Not modeled; observed robust initialization or EOWNERDEAD marks incomplete; operations still forwarded |
| Interprocess locks | Not modeled; observed process-shared initialization marks incomplete |
| Static linking / hidden/direct libc calls | Preload cannot observe operations bypassing dynamic interposition |
| Fork with active state | Child PID mismatch bypasses monitoring without taking inherited monitor mutexes; exec starts a new session; fork continuation monitoring is unsupported |
| Secure-execution/setuid binaries | Loader may restrict preload; not supported by this project workflow |
| Signal handlers / asynchronous cancellation | Unsupported |
| Arbitrary multiple preloads, dlclose of monitor | Not validated; monitor is intended to live for the process |

Capacity, inconsistent ownership, or internal failures emit one
`monitoring_incomplete` transition, with a reason/status. Further graph analysis
stops for that process; actual operation outcome records continue where the sink
works, marked incomplete. Their held snapshots may be stale and must not be
presented as reliable ownership. There is no in-process reset/recovery interface;
start a fresh process. `monitoring_complete=true` means no internal loss has been
identified within the supported event model, not proof of all-operation coverage
or absence of deadlocks. Unsupported APIs cannot all be detected automatically.

Historical request-order cycles remain **potential risks**, including when a
request fails or transactions execute sequentially. The controlled deadlock test
adds independent held/request/futex evidence; the monitor warning by itself does
not establish a current deadlock.

## Output and configuration

- `RDP_ENABLED`: absent or `1` enables monitoring; `0` forwards operations without
  records. Other values disable monitoring and emit a configuration diagnostic.
- `RDP_EVENT_FD`: optional decimal descriptor >= 2, already open for writing.
  The monitor duplicates it with close-on-exec; it does not close the caller's FD.
  Invalid numbers, closed FDs, or read-only FDs fall back to stderr and mark
  monitoring incomplete. No format variable is needed: output is always JSONL.
- Default: stderr. Application stdout remains separate. Application stderr can
  mix with this fallback, so a dedicated descriptor is required for reliable
  machine consumption.

The logger serializes full records and writes them synchronously, outside graph
locks. A slow sink can exert backpressure and delay application threads; no
asynchronous logger is claimed. Actual full-logging overhead is measured in PERFORMANCE_EVALUATION.md. Keep a pipe reader draining.
A nonblocking full pipe or broken sink disables delivery, reports `sink_error`
to stderr if possible, and leaves pthread results unchanged. SIGPIPE is blocked
only in the writing thread; a newly generated signal is consumed before restoring
that thread's old mask. The application's process-wide signal disposition is not
changed. If both sinks fail, no software can guarantee diagnostic delivery.

## Exact Ubuntu/WSL commands

Run from the `deadlock-sentinel` directory in a Linux terminal:

```bash
make monitor-build
make monitor-safe
make monitor-unsafe
make check-phase4
make monitor-deadlock   # bounded parent harness only; never direct fixture launch
```

For a dedicated JSONL file, independently of the Make run targets:

```bash
make monitor-build banking-build
RDP_EVENT_FD=3 LD_PRELOAD=./build/libdeadlock_monitor.so \
  ./build/banking_safe 3>build/safe-events.jsonl
RDP_EVENT_FD=3 LD_PRELOAD=./build/libdeadlock_monitor.so \
  ./build/banking_unsafe 3>build/unsafe-events.jsonl
RDP_ENABLED=0 LD_PRELOAD=./build/libdeadlock_monitor.so ./build/banking_safe
```

Use the relative preload value above in this workspace: its parent directory
contains a space. The loader splits LD_PRELOAD values on spaces/colons, even when
the shell assignment is quoted. An absolute path works only if that library path
contains neither separator. Do not export LD_PRELOAD globally to the shell or run
the intentional deadlock without its parent harness.

Tests save actual banking/deadlock records under `build/runtime-tests/`; the
optional `make check-phase4-ubsan` uses `build/runtime-tests-ubsan/`. Full regression
results are in VERIFICATION.md. The implemented FastAPI backend consumes the dedicated FD according to EVENT_SCHEMA.md; React displays its real projections. See FINAL_ARCHITECTURE.md.
