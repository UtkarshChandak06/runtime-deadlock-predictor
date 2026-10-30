# User-source workspace verification

Verified locally on 30 October 2026 (Asia/Kolkata), using Ubuntu/WSL2,
real GCC, Bubblewrap, systemd user scopes, the C preload monitor, FastAPI
and the React browser application. No result below is substituted from a fixture.

## Checks executed

- `DS_TEST_ISOLATION=1 make check-final` passed. This includes the original
  phases 1-4 runtime gates, 27 backend tests without skips, the production
  frontend build, 30 frontend tests, a 14-case real detection matrix, and
  five Playwright browser tests. Log: `build/workspace-final-check.log`.
- An additional real process test,
  `DS_TEST_ISOLATION=1 PYTHONPATH=backend:tests/backend backend/.venv/bin/python
  -m unittest test_workspace.UserCodeIntegration.test_abrupt_backend_restart_cleans_live_scope -v`,
  passed. It kills a backend worker while an isolated program is running,
  reopens the same store and verifies stopped/incomplete state and scope cleanup.
  Log: `build/workspace-restart-test.log`. This test is now included in future
  backend suite runs, making that suite 28 tests.
- Browser verification exercised paste, compile, monitor, graph, report export,
  edit/stale-build prevention, multiple uploaded source files, rerun, replay and
  reload. Pasted HTML remained editor text. Original example workflows also passed.
  Screenshots were inspected at `frontend/test-results/user-code-built.png` and
  the browser also saved `frontend/test-results/user-code-replay.png`.
- Real isolation tests exercised resource exhaustion (memory, processes, scratch
  and file size), blocked filesystem/network access, output bounds, cancellation,
  crashes, a nonzero exit distinct from a signal, timeout, malformed events,
  capacity loss, concurrent builds, persistence and artifact-integrity checks.
- `git diff --check` passed. No commit, push or publication was performed.

## Two submitted programs

`python3 scripts/demo_user_code.py` submitted, compiled and ran the source files
under `examples/user_sources/` through the HTTP workspace API. Both runs completed
with complete monitoring within the supported scope:

| Source | Actual events | Actual warnings | Session ID |
| --- | ---: | ---: | --- |
| consistent.c | 22 | 0 | d22b496f-f464-43a6-9b35-53de92ddc1d9 |
| conflicting.c | 20 | 1 | 76446867-d16a-47ed-99fc-b32b02cadd47 |

Exact source revisions, artifact hashes, session metadata and observed graph/event
data are saved in `build/user-code-demo.json`. The default workspace retains
these two projects and sessions until explicitly deleted.

## Interpretation and limits

Zero warnings do not prove safety. The conflicting run completes: its warning is
a potential cycle in historical lock ordering, not proof of a current deadlock.
Timeout alone is never a confirmed-deadlock verdict for submitted code.

Source call-site resolution is not implemented; the UI says "source location
unavailable". Supported mutex operations and finite monitor capacities are listed
in [the setup and usage manual](YOUR_CODE.md). Unsupported synchronization can be
invisible. Same-process instrumentation can be suppressed or forged by malicious
code. Isolation shares the Linux kernel and is intended for local use; these tests
are not a security certification. Benchmarks and sanitizer targets were not rerun
for this workspace change.
