# Banking simulator — Phase 2

## The basic model

An account is a struct containing a unique positive ID, a nonnegative `int64_t`
balance in cents, and its own POSIX mutex. Integer cents avoid rounding errors:
100000 cents represents 1000.00. A worker thread represents a transaction handler.
Both handlers run in one process and share the same two accounts.

A mutex allows one thread at a time to use a protected resource. The critical
section is the code between acquiring both account locks and releasing them.
Balance validation and both updates happen together in that section. Otherwise,
two workers could overwrite each other's changes or check outdated balances.

The shared `transfer` function rejects null/same accounts, duplicate or invalid
IDs, nonpositive amounts, invalid ordering modes, insufficient funds, and overflow.
Accounts must be initialized before use; IDs remain immutable during concurrent
execution. It checks `destination > INT64_MAX - amount` before addition. Rejected
transfers leave balances unchanged and release any acquired locks. No arbitrary
external pointers or uninitialized mutexes are supported.

## Three real workloads

| Workload | Worker 1 | Worker 2 | Coordination and outcome |
| --- | --- | --- | --- |
| Safe | Lock 1 then 2; transfer 10000 from 1 to 2 | Lock 1 then 2; transfer 15000 from 2 to 1 | Both workers started before joins; completes |
| Unsafe | Lock 1 then 2; transfer 10000 from 1 to 2 | Lock 2 then 1; transfer 15000 from 2 to 1 | Join worker 1 before creating worker 2; completes |
| Controlled deadlock | Hold 1, request 2 | Hold 2, request 1 | Barrier after first locks; both block; parent kills process |

In the safe strategy every nested acquisition moves from a smaller account ID to
a larger one. It cannot circle back to a smaller ID, preventing a circular wait
among these account locks. Transfer direction does not change the locking order.
The scheduler may run one worker to completion before the other; concurrent
creation does not promise simultaneous execution.

The unsafe strategy acquires the source account first. Opposite transfers produce
the real nested orders 1 -> 2 and 2 -> 1. Sequential joins keep these critical
sections from overlapping for this demonstration. This scheduling protection
does not make source-first locking safe in a general concurrent application.
The run completes; it has not actually deadlocked. A historical graph cycle signals
potential risk, not proof of a current deadlock.

## Controlled deadlock and evidence

Each worker successfully locks its source and prints a `HELD` marker. A two-party
pthread barrier ensures both own their first locks before printing `REQUEST` and
calling the second blocking mutex lock. The barrier uses no extra application
account locks. Neither worker can reach its unlock until its second lock succeeds.
Main calls pthread_join and consequently waits too. No balances are updated in
this fixture because neither worker reaches a two-lock critical section.

Only launch via:

```bash
make banking-deadlock-build
python3 tests/test_banking.py --deadlock-only
```

The Python parent starts an isolated process group and captures unbuffered C
output in a temporary file. At 2.5 seconds it checks the expected ownership/request
markers and reads all three Linux task `wchan` values. They must indicate futex
waits. With the known fixture protocol, two distinct held locks, opposite requests,
and no release path, this establishes the intended circular wait. A timeout alone
is never accepted as proof. This reasoning applies to this controlled fixture,
not arbitrary programs; there is no mutex interception or detection engine.

In a finally block the parent kills the whole child group with SIGKILL and waits
to reap its child. It verifies that the PID and process group are gone. SIGINT and
SIGTERM also trigger cleanup. SIGKILL of the parent or host failure cannot be
handled by Python. The fixture's command-line guard prevents accidental ordinary
invocation, but is not a security boundary. There is no direct-run Make target.

## Errors, tests, and scope

Pthread calls return error codes directly. Transfer validation failures release
locks normally. Unrecoverable synchronization failures print diagnostics and stop
the process rather than continue with uncertain ownership. On a partial deadlock
worker-creation failure, fail-fast exit is necessary: joining the first worker
would hang at its barrier. Normal completed workloads join workers and destroy
their account mutexes. The intentionally blocked process is reclaimed by the OS;
it cannot reach normal mutex destruction.

`make check-phase2` builds all workloads and C boundary tests, runs 20 safe and 20
unsafe executions, checks actual computed totals/final balances and nested lock
orders, tests invalid CLI inputs, and runs three bounded deadlock trials. It also
runs the Phase 1 regression script. Boundary tests cover zero/negative amounts,
same/invalid accounts, insufficient funds, destination overflow, exact integer
limits, and successful reuse after rejections.

The extra `tests/test_banking_validation.c` directly exercises the shared C API;
the Python harness tests process behavior. No third-party packages are needed.
Pthread API failures are not fault-injected, and this small fixed workload is not
a stress benchmark. Linux `/proc` visibility is required for deadlock verification.

Phase 3 now provides a separately tested explicit-event graph-analysis library.
Its tests supply the banking scenarios' conceptual safe and conflicting orders;
the banking sources themselves remain unchanged. Phase 4 now observes their real
supported mutex calls automatically when launched with its preload shared library.
See GRAPH_ANALYSIS.md.
