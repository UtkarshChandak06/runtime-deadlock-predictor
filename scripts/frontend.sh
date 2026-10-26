#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -x .tools/node/bin/node ]; then
    export PATH="$PWD/.tools/node/bin:$PATH"
fi
command -v node >/dev/null || { echo 'Linux Node missing. Run python3 scripts/install_node.py (local, no sudo).'; exit 1; }
cd frontend
exec npm "$@"
