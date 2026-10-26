# Detection evaluation — 2026-10-26

Actual records: [20261026T143514Z](../evidence/detection/20261026T143514Z/evaluation.json), [matrix CSV](../evidence/detection/20261026T143514Z/matrix.csv). Each case used the real preload monitor and bounded subprocess harness; stdout and JSONL are preserved alongside the matrix. All 14 expected-versus-observed cases passed. This is a scenario matrix, not a prediction-accuracy percentage.

| Scenario | Expected warnings | Observed | C incomplete | Cycle path entries | Outcome |
| --- | ---: | ---: | --- | ---: | --- |
| consistent_order | 0 | 0 | False | 0 | completed |
| sequential_inversion | 1 | 1 | False | 3 | completed |
| three_lock_cycle | 1 | 1 | False | 4 | completed |
| eight_lock_cycle | 1 | 1 | False | 9 | completed |
| controlled_deadlock | 1 | 1 | False | 3 | bounded_actual_deadlock_verified |
| recursive_valid | 0 | 0 | False | 0 | completed |
| unknown_static_normal | 0 | 0 | False | 0 | completed |
| unknown_static_recursive | 1 | 1 | True | 2 | completed |
| duplicate_closing_edge | 1 | 1 | False | 3 | completed |
| failed_errorcheck_operations | 1 | 1 | False | 2 | completed |
| graph_capacity | 0 | 0 | True | 0 | completed |
| identity_capacity | 0 | 0 | True | 0 | completed |
| unsupported_trylock | 0 | 0 | True | 0 | completed |
| unsupported_robust | 0 | 0 | True | 0 | completed |

A three-lock cycle has four path entries because the closing lock repeats; the eight-lock cycle has nine. These fixtures acquire each pair sequentially, so they validate historical cycle construction without deadlocking. Duplicate closing edges produce one warning.

The failed error-check operations include an EDEADLK self-request and EPERM unlock. The historical request can warn although the call fails; successful held-state updates remain separate. Explicit recursive locking produces no spurious warning. An unknown static recursive mutex cannot be safely assumed recursive, so its conservative self-cycle and incomplete annotation are intentional, not proof of a current deadlock. Unknown static normal use remains labelled unknown even when the observed run is otherwise complete.

Node/identity capacity and unsupported trylock/robust cases produce incomplete evidence. Other boundaries, edge/held-set capacities, address generations, invalid configuration, failed sinks and concurrent framing are exercised by the full Phase 3/4 regressions. This matrix does not claim every unsupported API is intercepted or that missing events are always detectable.

The controlled fixture emits its C cycle before timeout; the bounded harness additionally verifies successful first locks, conflicting requests, all three expected futex waiters, kill/reap and process-group absence. Its pre-kill C records can have monitoring_complete=true while the backend's missing normal end makes ingestion and overall completeness false. Those are different layers, not contradictory results.

Helgrind comparison: not run because valgrind is not installed. No substitute comparison or invented tool output is supplied. Reproduce with `make check-detection`; full supporting boundary checks are in `make check-final`.
