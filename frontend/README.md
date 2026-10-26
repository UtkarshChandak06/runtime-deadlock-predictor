# Deadlock Sentinel dashboard — Phase 6

React + TypeScript + Vite dashboard connected to the existing local FastAPI/C
monitor. All nine navigation sections work, including saved Performance measurements. There are no runtime fixture events
or fake metrics in production code. Unit fixtures are confined to tests/.

## Install and launch in Ubuntu/WSL

From the repository root, use Linux Node 24.15+ (24.22.1 was verified). If Linux
Node is absent, this optional command installs a pinned, SHA256-checked official
Node distribution in ignored `.tools/`, without sudo or system modifications:

```bash
python3 scripts/install_node.py
make frontend-install
make backend-install   # only if backend/.venv is not already installed
```

The Make targets select the project-local Node automatically. To use npm directly:

```bash
export PATH="$PWD/.tools/node/bin:$PATH"
cd frontend
npm run build
npm run test
```

Use two Ubuntu/WSL terminals at the repository root:

```bash
# Terminal 1
make backend-run
# Terminal 2
make frontend-run
```

Open **http://127.0.0.1:5173** (or http://localhost:5173) in Windows Chrome.
WSL localhost forwarding was verified here. Both servers bind loopback only;
do not change them to 0.0.0.0 or expose the API remotely. Vite uses strict port
5173 so it cannot silently choose an origin rejected by the backend. If Windows
cannot reach WSL localhost, check WSL localhost forwarding or use a browser in
the same Linux environment; do not loosen backend Origin/Host controls.

The optional `.env` can copy `.env.example`:
`VITE_API_BASE_URL=http://127.0.0.1:8000`. HTTP and WS use the same configured
base; no proxy hides origins. The backend explicitly permits the two frontend
origins above. Vite serves local code/assets; the dashboard needs no internet
after installation. No remote fonts, image services or telemetry endpoints are used.

## Checks

```bash
make frontend-build   # TypeScript then Vite production build
make check-phase6     # build plus Vitest / React Testing Library
make check-phase6-e2e # owns and cleans up real backend/Vite processes
```

The E2E runner refuses occupied ports 8000/5173 rather than terminating unrelated
servers. In this WSL setup it uses the installed Windows Node/Chrome to test the
Linux servers. On native Linux, install a supported Playwright Chromium beforehand
(`npm exec playwright install chromium` from frontend with Linux Node active).
If OS browser libraries are missing, ask your administrator to install them;
the project does not perform privileged package installation. With your own
running local servers, `npm run test:e2e` runs only the browser tests.

## Layout

```text
frontend/
  src/
    api/           typed REST models, lossless JSON and URL configuration
    hooks/         selected-session socket and recovery lifecycle
    lib/           cursor reducer and evidence-only replay projection
    components/    graph, threads, alerts, timeline, replay, reports, shared UI
    pages/         approved launcher and retained-session list
    App.tsx        navigation, catalog and selected-session shell
    main.tsx       React entry
    styles.css     responsive dark theme, Tailwind integration, print rules
  tests/           explicit unit/component/connection fixtures and tests
  e2e/             real Chrome + FastAPI + C monitor tests
  package.json / package-lock.json
  vite.config.ts / tsconfig.json / playwright.config.ts
  .env.example
```

See [architecture](../docs/FRONTEND_ARCHITECTURE.md),
[user guide](../docs/DASHBOARD_USER_GUIDE.md), and
[verification](../docs/PHASE6_VERIFICATION.md).
