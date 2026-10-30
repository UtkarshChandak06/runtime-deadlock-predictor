# Final limitations

- Historical cycles indicate potential risk. Sequential inversions and failed requests can warn without an actual deadlock. No warning does not prove safety; unexecuted paths and incomplete observation remain unknown.
- Actual-deadlock confirmation is restricted to the known barrier-controlled banking fixture, with request/ownership markers, Linux futex evidence and bounded lack of progress. A timeout alone is insufficient.
- Linux dynamic pthread interposition is required. Static linking, alternative runtimes, arbitrary fork continuation, condition-variable semantics, timed/try locks and robust recovery are not comprehensively tracked. Unknown mutex kinds are conservative.
- Graph, held set, registry, projections, queues and output/history retention are bounded. Truncation, parse failure, sequence loss, unsupported behavior and abnormal endings can invalidate completeness. Replay cannot restore evicted records.
- Synchronous logging perturbs timing and may block on a slow sink. Tiny-critical-section benchmarks show very large overhead. The default monitor is unoptimized; no performance optimization claim is made.
- Benchmarks use one WSL2 host, five measured repetitions, no CPU pinning/frequency control, short baselines and potentially false-sharing independent counters. Buffered file output has no fsync guarantee. `/dev/null` still includes analysis, formatting and writes. Backend/browser latency is not measured.
- The workspace extension persists bounded projects, builds and run snapshots, but does not provide durable WebSocket delivery or recovery of evicted events. There is no multi-worker coordination, authentication, arbitrary executable upload or production security certification. See YOUR_CODE.md for the isolated source-upload boundary and its limitations.
- Browser checks use Chrome on this Windows/WSL host at desktop and narrow widths. No formal accessibility audit, cross-browser certification or second-machine installation validation is claimed.
- Helgrind is not installed, so no Helgrind comparison is claimed. Applicable sanitizer outcomes are recorded in the final test report.
- Screenshots and saved measurements are recorded evidence, not currently running sessions. Unit fixtures are confined to tests; live scenarios and benchmark data use real processes.

These bounds support a technically defensible university demonstration, not deployment as a production deadlock detector.
