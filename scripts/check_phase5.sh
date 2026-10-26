#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make monitor-build banking-build
test -x backend/.venv/bin/python || { echo 'Run make backend-install first'; exit 1; }
PYTHONPATH=backend backend/.venv/bin/python -m unittest discover -s tests/backend -v
