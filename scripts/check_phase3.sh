#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
for tool in gcc ar make timeout; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
make analysis-tests
for test in graph tracker analyzer; do
    timeout 15s "./build/test_$test"
done
for ((i = 0; i < 20; ++i)); do
    timeout 15s ./build/test_analyzer_concurrent
done
echo 'PASS: Phase 3 unit suites and 20 repeated concurrent runs'
