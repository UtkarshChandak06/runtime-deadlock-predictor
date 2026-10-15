# Phase 6/7 Verification

Verified and audited on 2026-10-15. This report records observed results; it does not claim
that one synthetic benchmark predicts overhead for every application.

## Preceding-phase and regression verification

From a clean tree of build outputs, the following commands completed under WSL2:

```sh
make clean
make
make run-bare
make run
make check-diagnostics
make check-sanitizers
make check-robustness
make benchmark
```

The clean build emitted no warnings. All unit and integration tests passed in
bare and preloaded modes. The diagnostics harness passed normal, cycle,
capacity, output-failure, broken-pipe, false-positive, and bounded real-deadlock
cases. Five unit programs passed under ThreadSanitizer and under the combined
AddressSanitizer/UndefinedBehaviorSanitizer build. Resolver fault injection
passed recursive resolution, missing lock/unlock symbol, `/dev/full`, closed
pipe, concurrency, and no-state-mutation checks. No test was skipped or remained
inconclusive.

During verification, the TSan `log_cancel` test initially timed out because
TSan can intercept `write()` before the linker's `--wrap=write` hook. The test's
rendezvous was moved to the writer thread itself. The ordinary focused test,
focused TSan test, and complete sanitizer suite then passed. This changes test
synchronization only, not production logger behavior.

## Benchmark method and environment

`benchmarks/mutex_workload.c` uses `CLOCK_MONOTONIC` and validates its completed
iterations, operation count, protected counter, monitor presence or absence,
and clean graph/tracker/logger state. `benchmarks/run.py` calibrates each case,
alternates which mode runs first, records five successful samples per mode, and
computes medians. Startup, monitor initialization, mutex initialization,
thread creation/join, final probes, and validation are excluded. The timed
interval is the complete `work()` call for one thread; for multiple threads it
also consistently includes the start-release and completion barriers. The 13
cases produced 130 measurement records and no potential-deadlock warnings.

The audit raised the calibration ceiling from 3,200,000 to 25,600,000
iterations per thread. Calibration and measurement rows are now appended and
flushed as they complete; `environment.json` records `running`, `failed`, or
`complete` plus row counts. The runner independently rejects a result whose
requested iterations, completed iterations, counter, operation count, workload,
thread count, or monitor mode do not match. This prevents partial or invalid
runs from being presented as a complete result set.

- Platform: WSL2, Linux 6.18.33.2-microsoft-standard-WSL2, x86-64
- CPU: 12th Gen Intel Core i5-12450H, 12 logical CPUs
- Compiler: GCC 15.2.0, `-Wall -Wextra -O2 -g`
- Python: 3.14.4
- Samples: 5 per mode; calibration target 0.1 s; cap 25,600,000 iterations

No case hit the new cap. Two measurement medians nevertheless fell slightly
below 0.1 seconds (`independent/4`, 0.0968 s; `ordered/2`, 0.0834 s) because
calibration uses one sample and timings vary. They are explicitly flagged in
`summary.csv`. WSL scheduling, CPU frequency, thermal state, and other host
activity were not controlled; coefficients of variation range from 0.35% to
12.45%, so the figures are observations on this host, not universal constants.

### Audit of the approximately 0.02-second baseline results

The original 3,200,000-iteration single-thread baseline was valid but too short
for the 0.1-second target. Five direct audit reruns took 0.0200--0.0211 seconds,
each reporting 3,200,000 completed iterations, counter value 3,200,000, and
6,400,000 mutex operations. Disassembly of the `-O2` binary shows calls through
the PLT to `pthread_mutex_lock` and `pthread_mutex_unlock` inside the loop, plus
the counter update; the calls were not removed or folded. The executable has
undefined dynamic pthread lock/unlock symbols, so `LD_PRELOAD` can interpose
them. Roughly 6.5 ns per uncontended lock/unlock pair is plausible for glibc's
single-thread fast path on this host. The new run uses 25,600,000 iterations for
these cases and measures about 0.16 seconds, reducing timer and scheduling
sensitivity without changing the production monitor.

## Actual median results

| Workload | Threads | Iterations/thread | Baseline (s) | Monitored (s) | Difference (s) | Overhead |
|---|---:|---:|---:|---:|---:|---:|
| single | 1 | 25,600,000 | 0.162839 | 1.154974 | 0.992135 | 609.27% |
| independent | 1 | 25,600,000 | 0.163657 | 1.156319 | 0.992661 | 606.55% |
| independent | 2 | 12,800,000 | 0.175253 | 0.606262 | 0.431009 | 245.94% |
| independent | 4 | 6,400,000 | 0.096791 | 0.338617 | 0.241826 | 249.84% |
| independent | 8 | 6,400,000 | 0.129887 | 0.445457 | 0.315570 | 242.96% |
| shared | 1 | 25,600,000 | 0.165451 | 1.146950 | 0.981499 | 593.23% |
| shared | 2 | 1,600,000 | 0.148769 | 0.353025 | 0.204256 | 137.30% |
| shared | 4 | 800,000 | 0.149345 | 0.434372 | 0.285027 | 190.85% |
| shared | 8 | 400,000 | 0.178042 | 0.575021 | 0.396978 | 222.97% |
| ordered | 1 | 12,800,000 | 0.191792 | 3.094918 | 2.903126 | 1513.68% |
| ordered | 2 | 400,000 | 0.083447 | 0.752534 | 0.669087 | 801.81% |
| ordered | 4 | 400,000 | 0.175165 | 1.243516 | 1.068351 | 609.91% |
| ordered | 8 | 100,000 | 0.102541 | 0.956581 | 0.854040 | 832.88% |

Artifacts for this run:

- Raw samples: `benchmarks/results/20261015T224443+0530/raw.csv`
- Calibration: `benchmarks/results/20261015T224443+0530/calibration.csv`
- Summary and variability: `benchmarks/results/20261015T224443+0530/summary.csv`
- Host/compiler/status record: `benchmarks/results/20261015T224443+0530/environment.json`
- Median chart: `benchmarks/results/20261015T224443+0530/median.svg`

The original `20261015T222813+0530` directory is retained unchanged for audit
history. Its results are superseded because its 3,200,000-iteration cap left
seven baseline medians below the requested duration and did not flag them in the
summary. `20261015T222750+0530` is an empty directory from an aborted invocation;
it contains no CSV, environment record, or measurement to report. The audited
runner now writes a `running` environment record immediately after creating a
result directory, so a future early failure is explicitly identifiable.

## Robustness classification and remaining risks

- `pthread_mutex_lock` and `pthread_mutex_unlock`: supported and tested for
  ordinary mutexes; native return codes are preserved and failed calls do not
  alter tracking.
- Recursive mutexes: partially supported. Depth and return behavior are tested,
  but type-blind attempted-acquisition analysis can emit a conservative
  self-cycle warning.
- Priority-inheritance mutexes: partially supported. Ordinary lock/unlock passed;
  priority scheduling and inversion behavior were not tested.
- Robust mutexes: partially observed but unsupported for accurate tracking.
  `EOWNERDEAD` is forwarded, while the acquired ownership is not recorded.
- `pthread_mutex_trylock`, `pthread_mutex_timedlock`, condition waits, mutex
  initialization/destruction, static executables, and cross-process locking:
  unsupported because they are not intercepted. This can omit or retain graph
  relationships and therefore cause incomplete predictions.

The graph is bounded to 1024 nodes and 4096 edges; each thread tracks 64
distinct held locks; the logger queues 16 records. Exhaustion is bounded and
reported as incomplete monitoring. Address reuse after mutex destruction cannot
be distinguished. Historical order cycles indicate potential deadlocks, not
proof of a live deadlock: sequentially infeasible orders, mutex semantics, and
unsupported operations can produce false positives or false negatives.

Resolver failure now fails closed with `ENOSYS`, bypasses the queued logger for
emergency diagnostics, and avoids graph/tracker mutation. Remaining unsupported
contexts include fork with active state, signal-handler use, asynchronous
cancellation, and unloading an active monitor.
