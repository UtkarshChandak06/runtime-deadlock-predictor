#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/frontend.sh run build
bash scripts/frontend.sh run test
