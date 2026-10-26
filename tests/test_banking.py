#!/usr/bin/env python3
"""Linux-only, standard-library parent harness. Never launch the fixture alone."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def interrupt(signum, frame):
    raise KeyboardInterrupt(f"interrupted by signal {signum}")


def run_child(name, deadlock=False, extra=()):
    """Use a process group and file output, avoiding pipe-buffer deadlocks."""
    command = [str(ROOT / "build" / name), *extra]
    if deadlock:
        command.append("--controlled-fixture")
    with tempfile.TemporaryFile(mode="w+t") as capture:
        # Defer interruption until Popen returns and cleanup owns the PID.
        # Otherwise a signal between fork and assignment can orphan the child.
        previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK,
                                               {signal.SIGINT, signal.SIGTERM})
        child = None
        timed_out = False
        waits = []
        try:
            child = subprocess.Popen(command, stdout=capture, stderr=capture,
                                     start_new_session=True, cwd=ROOT)
            signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
            try:
                child.wait(timeout=2.5 if deadlock else 5)
            except subprocess.TimeoutExpired:
                timed_out = True
                if deadlock:
                    # Main is joining; both workers must be asleep in futex waits.
                    tasks = Path(f"/proc/{child.pid}/task")
                    waits = [(task / "wchan").read_text().strip()
                             for task in tasks.iterdir()]
        finally:
            # Kill the entire isolated group, including on Ctrl+C or SIGTERM.
            if child is not None:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()  # reap, never leave a zombie
            signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
        capture.seek(0)
        output = capture.read()
    require(not Path(f"/proc/{child.pid}").exists(), "child still exists after reap")
    try:
        os.killpg(child.pid, 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError("child process group still exists")
    if deadlock:
        required = ["HELD worker=1 account=1", "HELD worker=2 account=2",
                    "REQUEST worker=1 held=1 wanted=2", "REQUEST worker=2 held=2 wanted=1"]
        lines = output.splitlines()
        require(timed_out, f"fixture exited instead of blocking:\n{output}")
        require(child.returncode == -signal.SIGKILL, "fixture not killed by harness")
        require(all(lines.count(line) == 1 for line in required), output)
        require(max(lines.index(line) for line in required[:2]) <
                min(lines.index(line) for line in required[2:]), "barrier evidence missing")
        require(len(waits) == 3 and all("futex" in wait for wait in waits),
                f"expected three futex waits, got {waits}; timeout alone is inconclusive")
        require("UNEXPECTED" not in output and "FINAL" not in output, output)
        print(output, end="")
        print(f"WAIT_EVIDENCE tasks={len(waits)} wchan={','.join(waits)}")
        print("Controlled test: expected deadlock reached")
        print("Child process terminated by test harness; reaped; process group absent")
    else:
        require(not timed_out, f"unexpected timeout: {name}\n{output}")
    return child.returncode, output


def check_banking(name, unsafe):
    code, output = run_child(name)
    require(code == 0, output)
    initial = re.search(r"INITIAL account1=(\d+) account2=(\d+) total=(\d+)", output)
    final = re.search(r"FINAL account1=(\d+) account2=(\d+) total=(\d+)", output)
    require(initial is not None and final is not None, output)
    a, b, before = map(int, initial.groups())
    x, y, after = map(int, final.groups())
    require(a + b == before == x + y == after, "funds changed")
    require((x, y) == (a - 10000 + 15000, b + 10000 - 15000), "wrong transfers")
    require(x >= 0 and y >= 0 and "VALIDATION PASS" in output, output)
    require(len(re.findall(r"^TRANSFER ", output, re.M)) == 2, output)
    for worker, expected in [(1, [1, 2]), (2, [2, 1] if unsafe else [1, 2])]:
        sequence = re.findall(rf"^(ACQUIRED|RELEASED) worker={worker} account=(\d+)$", output, re.M)
        require(sequence == [("ACQUIRED", str(i)) for i in expected] +
                [("RELEASED", str(i)) for i in reversed(expected)], output)
    if unsafe:
        require(output.index("RELEASED worker=1 account=1") <
                output.index("ACQUIRED worker=2 account=2"), output)


def check_interruption(signum):
    """Interrupt a nested parent while its real fixture is alive; check cleanup."""
    parent = subprocess.Popen([sys.executable, __file__, "--deadlock-only"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, start_new_session=True)
    fixture_pid = None
    try:
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            children = Path(f"/proc/{parent.pid}/task/{parent.pid}/children").read_text().split()
            if children:
                fixture_pid = int(children[0])
                break
            time.sleep(0.01)  # Poll process creation; does not coordinate workers.
        require(fixture_pid is not None, "nested harness did not launch fixture")
        parent.send_signal(signum)
        output, _ = parent.communicate(timeout=5)
        require(parent.returncode != 0 and "interrupted by signal" in output, output)
        require(not Path(f"/proc/{fixture_pid}").exists(), "interruption leaked child")
        try:
            os.killpg(fixture_pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError("interruption leaked process group")
    finally:
        # Test failure must not itself leak a process.
        for pid in (fixture_pid, parent.pid):
            if pid is not None:
                try:
                    os.killpg(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        parent.communicate()
    print(f"PASS: {signal.Signals(signum).name} parent interruption leaves no fixture")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--deadlock-only", action="store_true")
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, interrupt)
    signal.signal(signal.SIGINT, interrupt)
    if not args.deadlock_only:
        for _ in range(20):
            check_banking("banking_safe", False)
            check_banking("banking_unsafe", True)
        print("PASS: 20 safe + 20 unsafe runs; balances, totals, nested orders, completion")
        code, output = run_child("test_banking_validation")
        require(code == 0, output)
        print(output.splitlines()[-1])
        for name in ("banking_safe", "banking_unsafe"):
            code, _ = run_child(name, extra=("invalid",))
            require(code != 0, f"{name} accepted invalid arguments")
        code, _ = run_child("banking_deadlock")
        require(code != 0, "fixture guard failed")
        print("PASS: invalid CLI arguments and direct-launch guard")
        check_interruption(signal.SIGINT)
        check_interruption(signal.SIGTERM)
    for _ in range(1 if args.deadlock_only else 3):
        run_child("banking_deadlock", deadlock=True)
    print("PASS: Phase 2 controlled workload checks")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, KeyboardInterrupt) as error:
        raise SystemExit(f"FAIL: {error}") from error
