# Phase 5 backend

Run inside Ubuntu/WSL from the repository root:

```bash
make backend-install
make backend-run
```

The installer uses Python's local `backend/.venv` (ignored by Git), bootstrapping
pip from a SHA256-verified PyPI wheel if Ubuntu lacks ensurepip. Installation
needs internet; normal execution does not. No system-wide pip or packages are
installed. Python 3.14.4 was verified. Direct dependencies are pinned in
`requirements.txt`; the verification document records the installed transitive set.

Open `http://127.0.0.1:8000/docs` for the interactive OpenAPI contract.
Stop with Ctrl+C; supervised process groups are terminated and reaped.
Use a single Uvicorn worker. The default .workspace store persists bounded projects, revisions, builds and session snapshots. A workspace lock prevents multiple backend owners.

```bash
curl -s http://127.0.0.1:8000/api/v1/applications
curl -s -H 'Content-Type: application/json' \
  -d '{"application_id":"banking_unsafe"}' http://127.0.0.1:8000/api/v1/sessions
make check-phase5
```

Modules in `app/`: `main.py` contains typed HTTP/WS routes and local security;
`config.py` defines bounds; `models.py` defines public contracts;
`process_runner.py` owns Linux descriptors, subprocesses and cleanup;
`event_parser.py` validates C JSONL v1; `session_manager.py` owns lifecycle and
bounded evidence; `graph_projection.py` projects actual observations;
`websocket_manager.py` provides bounded nonblocking subscriber queues.

See [API contract](../docs/API_CONTRACT.md),
[architecture](../docs/BACKEND_ARCHITECTURE.md), and
[actual verification](../docs/PHASE5_VERIFICATION.md).

Custom source endpoints and sandbox setup: [Your Code manual](../docs/YOUR_CODE.md). DS_WORKSPACE_DIR selects a trusted local store directory. Use /api/v1/capabilities to inspect supported scope and the actual isolation availability.
