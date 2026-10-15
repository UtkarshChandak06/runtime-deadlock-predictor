#!/usr/bin/env python3
"""Run isolated resolver fault injection; production code has no test knob."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
binary = root / "build" / ("robustness_resolver_before" if "--original" in sys.argv else "robustness_resolver")
env = os.environ.copy()
env.pop("LD_PRELOAD", None)
if "--original" in sys.argv:
    env["RDP_TEST_RESOLVER"] = "reenter"
    try:
        result = subprocess.run([str(binary)], env=env, capture_output=True, timeout=3)
    except subprocess.TimeoutExpired:
        print("REPRODUCED original resolver reentry: timed out after 3 seconds (killed and reaped)")
    else:
        raise AssertionError(f"Expected original reentry hang, got {result.returncode}")
    sys.exit(0)
for mode in ("reenter", "lock", "unlock"):
    env["RDP_TEST_RESOLVER"] = mode
    result = subprocess.run([str(binary)], env=env, capture_output=True, timeout=10, check=True)
    assert b"PASS" in result.stdout and b"FATAL" in result.stderr
    print(mode + ": " + result.stdout.decode().strip())
    with open("/dev/full", "wb") as sink:
        result = subprocess.run([str(binary)], env=env, stdout=subprocess.PIPE, stderr=sink, timeout=10, check=True)
        assert b"PASS" in result.stdout
    read_end, write_end = os.pipe()
    os.close(read_end)
    try:
        result = subprocess.run([str(binary)], env=env, stdout=subprocess.PIPE, stderr=write_end, timeout=10, check=True)
        assert b"PASS" in result.stdout
    finally:
        os.close(write_end)
    print(mode + ": PASS emergency diagnostics /dev/full and closed pipe")
print("PASS all resolver robustness cases")
