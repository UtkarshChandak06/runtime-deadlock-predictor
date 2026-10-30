# Your Code workspace

Deadlock Sentinel accepts user-supplied Linux C source and runs supported pthread
mutex operations through the existing real C monitor. Example workloads remain
under **Overview > Examples**. Custom source lives under **Your Code**. A program
name, source keyword or example description never determines an observed warning.

## Setup on Ubuntu or WSL

Use an unprivileged Linux account with systemd and a functioning systemd user
manager. This implementation was exercised on x86-64 Ubuntu/WSL2. It requires
GCC, Python 3, Bubblewrap 0.11 or newer, systemd-run/systemctl and prlimit.

```bash
# If needed, install through Ubuntu's package manager.
sudo apt-get install build-essential bubblewrap python3 util-linux
systemctl --user is-system-running
test -f /sys/fs/cgroup/cgroup.controllers
bwrap --version

# From the deadlock-sentinel repository root:
make backend-install
make frontend-install
make monitor-build banking-build
make backend-run
# In a second terminal:
make frontend-run
```

For WSL installations without systemd, enable systemd in the distribution's
`/etc/wsl.conf` (`[boot]` followed by `systemd=true`), then restart that distribution.
The user manager must support memory, swap, process-count and CPU controls under
cgroup v2. The backend probes actual applied scope properties and a real isolated
process. If this fails, Build and Run are disabled and the reason is shown.
There is no host-execution fallback. Examples continue to use their existing
trusted-workload supervisor.

Open http://127.0.0.1:5173 and select **Your Code**. API documentation is at
http://127.0.0.1:8000/docs. Keep one backend worker and one backend per workspace.
An exclusive workspace lock prevents two backends from editing the same store.

## Paste or upload

1. Load **Consistent ordering** or paste your own C program in the highlighted editor.
2. Add or rename editable file tabs. Upload individual `.c` and `.h` files; no archives.
   Use **New empty workspace** before uploading a complete project that includes
   `main.c`, to avoid duplicating the initially loaded file.
3. Use up to 8 files and 128 KiB of UTF-8 source in total. Paths must be relative,
   use `/` separators and contain no traversal, empty segments or duplicate
   case-insensitive names. At least one `.c` file is required.
4. **Save revision** stores an immutable content-addressed revision. **Build**
   saves the current source and compiles it. A later source edit marks the displayed
   build stale; run is disabled until rebuilding. The backend independently checks
   the current persisted revision and the requested revision against the build.
5. Inspect actual compiler output, file/line/column diagnostics, duration, exit code
   and truncation. A failed or stopped build cannot be executed.
6. Enter arguments as a JSON string array, for example `["input", "42"]`.
   They are literal arguments, not a shell command. At most 16 arguments of 256
   UTF-8 bytes each are accepted. Standard input is at most 8 KiB.
7. Set a wall-time limit of 1-30 seconds, then **Run with Monitoring**.
8. Inspect Live Monitor, Lock Graph, Thread Explorer, Alerts, Event Timeline and
   Reports. **Stop run** or **Stop session** terminates the owned cgroup. **Stop build**
   cancels compilation. Closing the browser alone does not cancel either operation.

The fixed compiler configuration is:

```text
gcc -std=c11 -Wall -Wextra -Wpedantic -g -O0 -fno-omit-frame-pointer
    -pthread -I/src /src/<each C source> -o /out/program
```

No compiler/linker flag input, plugins, Makefiles or build-script upload exists.
The one compiler shell script is a fixed repository string; filenames are separate
positional arguments. The source tree is read-only. Build IDs are server-generated.
Each successful build records its exact files/revision, flags, executable hash,
compiler binary hash, monitor hash and libc hash. Its executable and monitor copy
are immutable to the API. Run checks the stored hashes and rejects changed libc.
The monitor and compiler use the same Ubuntu `/usr` runtime. Rebuild after runtime
updates. Artifact hashes detect corruption, not malicious modification by the host owner.

## What is actually monitored

The C wrappers intercept `pthread_mutex_init`, `pthread_mutex_lock`,
`pthread_mutex_unlock` and `pthread_mutex_destroy`. Explicit recursive mutex depth
is tracked. Known robust or process-shared attributes mark monitoring incomplete.
Statically initialized mutex types can be unknown and are treated conservatively.
Trylock/timedlock, condition variables, semaphores, rwlocks, robust recovery,
static executables, asynchronous cancellation and fork/exec continuity are outside
complete coverage. Some unsupported operations are invisible and cannot be reliably
identified from this trace. No source-keyword scan is used to invent observations.

The graph holds at most 32 nodes and 128 edges; a thread holds at most 16 distinct
tracked locks; the runtime identity registry has 64 slots. Capacity failures are
reported as incomplete analysis. They are not a clean result. Historical edges
remain after unlock; BFS reconstructs a witness when a new edge closes a cycle.

Interpret results on two independent axes:

| Execution | Meaning |
| --- | --- |
| Build failed | No runnable build produced; inspect compiler diagnostics |
| Completed | User process returned zero, independently of monitoring completeness |
| Failed with crash_signal_N | Trusted parent observed termination by signal N |
| Failed with nonzero_exit | User process returned a nonzero status |
| Timed out | Supervisor reached the wall deadline; not proof of deadlock |
| Stopped | User or backend shutdown requested termination |
| Target status unavailable | Whole-scope termination/failure prevented the parent from returning status |

| Observation | Meaning |
| --- | --- |
| Potential lock-order cycle | C monitor reported a historical dependency cycle |
| No cycle observed | No cycle was reported in observed supported requests; not proof of safety |
| No relevant mutex activity | No supported lock request was observed |
| Incomplete monitoring | Missing end records, unsupported detected semantics, invalid/lost events or capacity failures |

A known example's fixture evidence is separate from generic analysis. Custom
timeouts are never promoted to confirmed deadlocks. A trusted parent inside the
sandbox reports the actual `wait` return status over its own FD, which is not
inherited by the user program. It disables dumpability to restrict procfs/ptrace
access to the parent's descriptors. The C monitor has a separate event pipe;
stdout and stderr cannot accidentally be parsed as events. All event records pass
the existing JSONL schema, identity, timestamp and sequence checks. Program events
use namespace PID 3; the dashboard labels the host runner PID separately.

Same-process monitoring is **not tamper-proof**: a program can close the event FD,
change its environment, invoke unobserved APIs or forge valid-looking events.
Treat results as debugging evidence for cooperative programs, not a security verdict.
Debug symbols are retained, but call-site relocation and symbol resolution are not
implemented; the UI reports **source location unavailable** without guessed lines.

## Isolation and limits

Both compilation and execution use Bubblewrap with separate user, PID, network,
IPC, UTS and cgroup namespaces. The process has UID/GID 1000 inside its user
namespace, no capabilities, no new privileges and disabled nested user namespaces.
The base root and `/usr` are read-only; only selected source files or individual
artifacts are mounted. No home, credentials, host `/etc`, container socket or host
workspace is mounted. `/proc` belongs to the PID namespace. The device filesystem
is read-only; bounded `/tmp` and `/out` tmpfs mounts provide scratch.

Per job: 256 MiB cgroup memory, zero swap, 32 tasks, 100% of one CPU quota, a
per-process CPU-time rlimit of wall limit plus one second, 8 MiB per-file size,
64 file descriptors and no core dumps. Scratch is 32 MiB `/tmp` plus 16 MiB `/out`.
Build wall time is 20 seconds; run wall time is 1-30 seconds. A systemd scope also
has a runtime watchdog three seconds above the requested bound. On normal exit,
stop, timeout, cancellation or exceptions the entire scope is stopped, including
descendants, and inactivity is checked. Scope IDs are saved before launch so a
restarted backend can stop its recorded jobs. An abrupt backend crash can leave
a scope alive until restart cleanup or the independent watchdog expires.

Combined program stdout/stderr is hard-bounded to 1 MiB; retained prefixes are
64 KiB per stream. Runtime event input is hard-bounded to 2 MiB, with the original
4096-byte per-record validation. Crossing a hard output/event budget terminates
the job and reports the limit. Binary extraction during build has an 8 MiB cap.
At most two builds and two runs are admitted concurrently by default.

This shares the host kernel and readable Ubuntu runtime. It is not a VM, a
security certification, or a multi-tenant remote execution service. It does not
protect against kernel/Bubblewrap/systemd vulnerabilities, side channels or a
malicious host owner. Keep the backend loopback-only; existing Host/Origin checks
apply to all endpoints. There is no authentication or remote deployment support.

## Persistence and deletion

The default local store is `.workspace/`, ignored by Git. `DS_WORKSPACE_DIR` can
select another trusted local directory before starting the backend. SQLite stores
projects, revisions, build metadata and retained run snapshots; artifacts use
server-generated directory names. Limits: 10 projects, 50 revisions, 20 builds,
64 MiB artifacts, a 64 MiB database, and at most 20 retained runs (or the configured
smaller session limit). Each persisted run snapshot is bounded to 2 MiB. Old event
records can be evicted explicitly with dropped counters and history-gap flags;
projection retention loss is marked. The default in-memory event window is 2000.
An exhausted quota requires explicit deletion; there is no silent eviction of a project.

**Sessions & Replay > Delete** removes a terminal run. **Your Code > Delete project
and retained runs** removes that project's revisions, builds, artifacts and runs.
Active jobs must first be stopped. Backend restart restores terminal sessions;
interrupted runs become stopped with `backend_restarted`, incomplete observations
and separately checked cleanup. Replay projects retained history without launching
anything. JSON export contains the exact source, build hashes, arguments, stdin,
runtime settings, coverage and retention limits. Keep these exports private if the
submitted source or input contains sensitive information.

## Two-program demonstration

Load **Consistent ordering**, Build, Run with Monitoring: both worker threads use
the same two-mutex order. Inspect actual zero-warning evidence and completed output.
Then load **Conflicting historical ordering**, Build, Run: the program completes
but its second nested request introduces the reverse historical edge. Inspect the
actual warning and cycle; replay the event sequence. Both are editable source
submissions, compiled into new artifacts, not catalog-specific result generators.

For a repeatable API demonstration with the backend running, execute
`python3 scripts/demo_user_code.py`. It submits the two files under
`examples/user_sources/`, prints links to their actual graph sessions and saves
real evidence in `build/user-code-demo.json`. It creates two retained projects;
delete them from the UI when no longer needed.

## Verification commands

```bash
make check-phase5                         # existing backend + validation tests
DS_TEST_ISOLATION=1 PYTHONPATH=backend backend/.venv/bin/python \
  -m unittest discover -s tests/backend -v
make check-phase6                         # build and frontend tests
make check-phase6-e2e                     # stop dev servers first; real browser tests
make check-phase4                         # original real monitor regression suite
```

The opt-in isolated tests must actually execute for isolation claims; skipped tests
do not establish containment. Browser tests require a working isolation provider
and use a temporary workspace so they do not delete a user's projects.
