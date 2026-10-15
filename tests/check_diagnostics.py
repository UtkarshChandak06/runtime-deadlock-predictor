#!/usr/bin/env python3
"""Capture real subprocess output, assert full warning records, enforce timeouts."""
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
results = root / "build" / "diagnostics"
results.mkdir(exist_ok=True)
env = dict(os.environ, LD_PRELOAD=str(root / "build/libdeadlock_monitor.so"))
heading = "[WARNING] Potential Deadlock Detected (not a confirmed deadlock)"
pattern = re.compile(r"\[WARNING\] Potential Deadlock Detected \(not a confirmed deadlock\)\n"
                     r"Thread: tid=(\d+)\nHeld Lock: (0x[0-9a-f]+)\n"
                     r"Requested Lock: (0x[0-9a-f]+)\nCycle: ([^\n]+)\n")

def run(name, mode=None, preload=True, sink=None):
    args = [str(root / "build" / name)] + ([mode] if mode else [])
    proc = subprocess.run(args, env=env if preload else {k:v for k,v in os.environ.items() if k != "LD_PRELOAD"},
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE if sink is None else sink,
                          text=True, timeout=15)
    label = name + ("-" + mode if mode else "") + ("-bare" if not preload else "")
    (results / (label + ".stdout")).write_text(proc.stdout)
    if proc.stderr is not None:
        (results / (label + ".stderr")).write_text(proc.stderr)
    assert proc.returncode == 0, (label, proc.returncode, proc.stderr)
    return proc

for name in ["t00_smoke", "t10_single_mutex", "t11_nested_reverse", "t12_two_threads_indep", "t20_multi_thread_order"]:
    p = run(name)
    assert not p.stderr, (name, p.stderr)
    print("PASS quiet normal/consistent locking:", name)
p = run("t30_diagnostics", "shared")
assert not p.stderr
print("PASS shared single mutex: 8 threads, zero diagnostics")

for mode, count in [("two", 1), ("three", 1), ("concurrent", 8), ("long", 1), ("new", 2)]:
    p = run("t30_diagnostics", mode)
    expected = re.findall(r"EXPECT tid=(\d+) held=(0x[0-9a-f]+) requested=(0x[0-9a-f]+) cycle=([^\n]+)", p.stdout)
    actual = pattern.findall(p.stderr)
    assert len(actual) == count and sorted(actual) == sorted(expected), (mode, expected, actual)
    assert not pattern.sub("", p.stderr), (mode, "interleaved or unexpected output")
    print("PASS complete warning fields/path and bounded dedup:", mode, count, "records")
    bare = run("t30_diagnostics", mode, preload=False)
    assert not bare.stderr
    print("PASS bare execution:", mode, "zero warnings")

for mode in ["tracker", "nodes", "edges"]:
    p = run("t30_diagnostics", mode)
    assert "INCOMPLETE" in p.stderr and heading not in p.stderr
    assert len(p.stderr.splitlines()) == 1
    print("PASS capacity warning and state:", mode)

for name, count in [("t13_failed_ops",1), ("t21_sequential_cycle",2)]:
    p = run(name)
    assert len(pattern.findall(p.stderr)) == count
    assert not pattern.sub("", p.stderr)
    print("PASS failed-operation regression:", name)

# A failed sink must not change return codes, errno, graph or tracker state.
with open('/dev/full', 'w') as sink:
    run("t30_diagnostics", "failure", sink=sink)
print("PASS ENOSPC logging failure; graph/tracker/errno assertions intact")
read_fd, write_fd = os.pipe()
os.close(read_fd)
try:
    run("t30_diagnostics", "failure", sink=write_fd)
finally:
    os.close(write_fd)
print("PASS broken pipe: SIGPIPE contained, graph/tracker/errno intact")

# Deliberate deadlock ONLY as a kill-and-reap subprocess with a strict timeout.
try:
    subprocess.run([str(root / 'build/real_deadlock')], env=env,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=2, check=True)
    raise AssertionError('intentional deadlock unexpectedly completed')
except subprocess.TimeoutExpired as exc:
    output = (exc.stderr or b'').decode()
    (results / 'real-deadlock.stderr').write_text(output)
    (results / 'real-deadlock.stdout').write_bytes(exc.stdout or b'')
    warnings = pattern.findall(output)
    assert len(warnings) == 1, output
    tid, held, requested, cycle = warnings[0]
    assert cycle == f'{requested} -> {held} -> {requested}'
    print('PASS real deadlock: complete warning before 2s timeout; child killed and reaped')
print('PASS false-positive demonstration: sequential two/three cycles warn and exit successfully')
print('TEST diagnostics PASS')

