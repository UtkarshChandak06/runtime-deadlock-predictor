#!/usr/bin/env bash
# Can be invoked from any working directory, including paths with spaces.
set -euo pipefail
trap 'echo "FAIL: Phase 1 check failed (line $LINENO)." >&2' ERR
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

for tool in gcc make grep wc timeout dirname; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "FAIL: required tool '$tool' is missing." >&2
        exit 1
    fi
done

make
# A timeout also catches a program that prints messages but never terminates.
output=$(timeout 10s ./build/hello_threads)
printf '%s\n' "$output"
for expected in \
    'Deadlock Sentinel - Phase 1' \
    'Worker thread 1 started' \
    'Worker thread 2 started' \
    'Both threads completed successfully'; do
    count=$(grep -Fxc -- "$expected" <<< "$output" || true)
    if [[ "$count" != 1 ]]; then
        echo "FAIL: expected exactly one occurrence of: $expected" >&2
        exit 1
    fi
done
if [[ $(printf '%s\n' "$output" | wc -l) -ne 4 ]]; then
    echo 'FAIL: unexpected extra output.' >&2
    exit 1
fi
echo 'PASS: both workers printed once and the program exited successfully.'
