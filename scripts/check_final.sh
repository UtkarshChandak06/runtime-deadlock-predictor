#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/check_phase1.sh
make check-phase2
make check-phase3
make check-phase4
make check-phase5
make check-phase6
make check-detection
make check-phase6-e2e
echo 'PASS final gate: all mandatory checks completed; benchmarks and sanitizers are separate targets.'
