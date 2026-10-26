#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make clean
make
make run
make banking-safe
make banking-unsafe
make check-phase2
make analysis-demo
make check-phase3
make monitor-safe
make monitor-unsafe
make check-phase4
make check-phase5
bash scripts/check_phase1.sh
