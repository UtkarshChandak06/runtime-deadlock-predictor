#!/usr/bin/env python3
"""Real LD_PRELOAD integration, isolated children, standard library only."""
import argparse
from contextlib import ExitStack
import errno
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = "./build/libdeadlock_monitor.so"
ARTIFACTS = ROOT / "build" / "runtime-tests"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def interrupt(signum, frame):
    raise KeyboardInterrupt(f"signal {signum}")


def records(text):
    return [json.loads(line) for line in text.splitlines()]


def validate(events, pid):
    previous_sequence = 0
    previous_time = 0
    for event in events:
        require(event["schema_version"] == 1, event)
        require(event["process_id"] == pid and event["thread_id"] > 0, event)
        require(event["sequence"] > previous_sequence, event)
        require(event["timestamp_ns"] >= previous_time and event["timestamp_ns"] > 0, event)
        require(isinstance(event["monitoring_complete"], bool), event)
        previous_sequence, previous_time = event["sequence"], event["timestamp_ns"]
        if "held_locks" in event:
            require(len(event["held_locks"]) == len(set(event["held_locks"])), event)
            require(len(event["held_locks"]) == len(event["held_depths"]), event)
            require(all(depth > 0 for depth in event["held_depths"]), event)
        if event["session_event"] == "potential_lock_order_cycle":
            cycle = event["cycle"]
            require(len(cycle) >= 2 and cycle[0] == cycle[-1], event)
            require(event["closing_edge"] == [cycle[-2], cycle[-1]], event)
            require(event["held_lock"] == cycle[-2] and event["requested_lock"] == cycle[0], event)


def run(name, args=(), *, preload=True, config=None, sink="file", deadlock=False, artifact=None):
    environment = {k: v for k, v in os.environ.items() if not k.startswith("RDP_") and k != "LD_PRELOAD"}
    if preload:
        require(not any(c.isspace() for c in LIBRARY), "LD_PRELOAD library path must contain no whitespace")
        environment["LD_PRELOAD"] = LIBRARY
    if config:
        environment.update(config)
    with ExitStack() as stack:
        out = stack.enter_context(tempfile.TemporaryFile())
        err = stack.enter_context(tempfile.TemporaryFile())
        events_file = stack.enter_context(tempfile.TemporaryFile())
        descriptors = []
        pipe_read = pipe_write = None
        if sink == "file":
            descriptors = [events_file.fileno()]
            environment["RDP_EVENT_FD"] = str(descriptors[0])
        elif sink in ("pipe", "broken", "full"):
            pipe_read, pipe_write = os.pipe()
            stack.callback(os.close, pipe_write)
            descriptors = [pipe_write]
            environment["RDP_EVENT_FD"] = str(pipe_write)
            if sink == "broken":
                os.close(pipe_read)
                pipe_read = None
            else:
                stack.callback(os.close, pipe_read)
            if sink == "full":
                os.set_blocking(pipe_write, False)
                try:
                    while True:
                        os.write(pipe_write, b"x" * 4096)
                except BlockingIOError:
                    pass
        elif sink == "invalid":
            environment["RDP_EVENT_FD"] = 'not-a-descriptor"\n'
        elif sink == "closed":
            environment["RDP_EVENT_FD"] = "999999"
        elif sink == "readonly":
            source = stack.enter_context(open(__file__, "rb"))
            descriptors = [source.fileno()]
            environment["RDP_EVENT_FD"] = str(source.fileno())
        elif sink != "stderr":
            raise ValueError(sink)
        child = None
        waits = []
        timed_out = False
        command = [str(ROOT / "build" / name), *args]
        previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGTERM})
        try:
            child = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=out, stderr=err,
                                     pass_fds=descriptors, start_new_session=True)
            signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
            try:
                child.wait(timeout=2.5 if deadlock else 15)
            except subprocess.TimeoutExpired:
                timed_out = True
                if deadlock:
                    waits = [(task / "wchan").read_text().strip()
                             for task in Path(f"/proc/{child.pid}/task").iterdir()]
        finally:
            if child is not None:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()
            signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
        require(child is not None, "child not launched")
        require(not Path(f"/proc/{child.pid}").exists(), "child not reaped")
        try:
            os.killpg(child.pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError("process group remains")
        out.seek(0); err.seek(0); events_file.seek(0)
        stdout = out.read().decode(); stderr = err.read().decode()
        raw = events_file.read().decode()
        if sink == "pipe":
            os.set_blocking(pipe_read, False)
            chunks = []
            try:
                while True:
                    chunk = os.read(pipe_read, 65536)
                    if not chunk:
                        break
                    chunks.append(chunk)
            except BlockingIOError:
                pass
            raw = b"".join(chunks).decode()
        if sink in ("stderr", "invalid", "closed", "readonly", "broken", "full"):
            raw = stderr
        events = records(raw)
        validate(events, child.pid)
        if artifact:
            ARTIFACTS.mkdir(parents=True, exist_ok=True)
            (ARTIFACTS / f"{artifact}.jsonl").write_text(raw)
            (ARTIFACTS / f"{artifact}.stdout").write_text(stdout)
        if deadlock:
            require(timed_out and child.returncode == -signal.SIGKILL, (stdout, stderr))
            lines = stdout.splitlines()
            expected = ["HELD worker=1 account=1", "HELD worker=2 account=2",
                        "REQUEST worker=1 held=1 wanted=2", "REQUEST worker=2 held=2 wanted=1"]
            require(all(lines.count(line) == 1 for line in expected), stdout)
            require(max(lines.index(x) for x in expected[:2]) < min(lines.index(x) for x in expected[2:]), stdout)
            require(len(waits) == 3 and all("futex" in wait for wait in waits), waits)
            requests = select(events, "lock_request")
            acquired = select(events, "lock_acquired")
            warning = select(events, "potential_lock_order_cycle")
            require(len(requests) == 4 and len(acquired) == 2 and len(warning) == 1, events)
            blocked = [e for e in requests if len(e["held_locks"]) == 1]
            require(len(blocked) == 2 and blocked[0]["thread_id"] != blocked[1]["thread_id"], blocked)
            require(blocked[0]["held_locks"] == [blocked[1]["lock_id"]] and
                    blocked[1]["held_locks"] == [blocked[0]["lock_id"]], blocked)
            require(len(warning[0]["cycle"]) == 3, warning)
            require(not select(events, "monitoring_incomplete"), events)
            print("PASS monitored deadlock: two held/opposite requests, full cycle before timeout, three futex waits; killed/reaped/group absent")
        else:
            require(not timed_out and child.returncode == 0, (name, args, child.returncode, stdout, stderr))
        if sink == "file":
            require(not stderr, f"unexpected stderr: {stderr}")
        return stdout, events


def select(events, kind):
    return [event for event in events if event["session_event"] == kind]


def balanced(events):
    held = {}
    for e in events:
        state = held.setdefault(e["thread_instance"], {})
        event = e["session_event"]
        if event == "lock_acquired":
            state[e["lock_id"]] = state.get(e["lock_id"], 0) + 1
        elif event == "lock_released":
            require(e["lock_id"] in state, e)
            state[e["lock_id"]] -= 1
            if not state[e["lock_id"]]:
                del state[e["lock_id"]]
        if "held_locks" in e and e["monitoring_complete"]:
            require(dict(zip(e["held_locks"], e["held_depths"])) == state, e)
    require(all(not state for state in held.values()), held)


def fixture(mode, **kwargs):
    stdout, events = run("test_runtime_mutex", (mode,), **kwargs)
    require("FIXTURE PASS" in stdout, stdout)
    observed = {int(tid) for tid in re.findall(r"tid=(\d+)", stdout)}
    require(observed <= {e["thread_id"] for e in events} or not events, (stdout, events))
    return stdout, events


def check_interruption(signum):
    parent = subprocess.Popen([sys.executable, __file__, "--deadlock-only", "--library", LIBRARY],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, start_new_session=True)
    fixture_pid = None
    try:
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            children = Path(f"/proc/{parent.pid}/task/{parent.pid}/children").read_text().split()
            if children:
                fixture_pid = int(children[0]); break
            time.sleep(0.01)
        require(fixture_pid is not None, "runtime parent did not launch fixture")
        parent.send_signal(signum)
        output, _ = parent.communicate(timeout=5)
        require(parent.returncode != 0 and "FAIL: signal" in output, output)
        require(not Path(f"/proc/{fixture_pid}").exists(), "interrupted runtime child not reaped")
        try:
            os.killpg(fixture_pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError("interrupted runtime group remains")
    finally:
        for pid in (fixture_pid, parent.pid):
            if pid is not None:
                try:
                    os.killpg(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        parent.communicate()
    print(f"PASS monitored harness {signal.Signals(signum).name}: child reaped, process group absent")


def main():
    global LIBRARY, ARTIFACTS
    parser = argparse.ArgumentParser()
    parser.add_argument("--deadlock-only", action="store_true")
    parser.add_argument("--library", default=LIBRARY)
    args = parser.parse_args(); LIBRARY = args.library
    if "ubsan" in LIBRARY:
        ARTIFACTS = ROOT / "build" / "runtime-tests-ubsan"
    signal.signal(signal.SIGINT, interrupt); signal.signal(signal.SIGTERM, interrupt)
    if args.deadlock_only:
        run("banking_deadlock", ("--controlled-fixture",), deadlock=True, artifact="deadlock")
        return
    for mode in ("safe", "unsafe"):
        baseline, _ = run(f"banking_{mode}", preload=False)
        for repetition in range(5):
            stdout, events = run(f"banking_{mode}", artifact=mode if repetition == 0 else None)
            require(re.search(r"FINAL .*", stdout)[0] == re.search(r"FINAL .*", baseline)[0], stdout)
            require("VALIDATION PASS" in stdout, stdout)
            require(len(select(events, "lock_request")) == 4 and len(select(events, "lock_acquired")) == 4 and
                    len(select(events, "lock_released")) == 4, events)
            warnings = select(events, "potential_lock_order_cycle")
            require(len(warnings) == (1 if mode == "unsafe" else 0), warnings)
            ids = {e["lock_id"] for e in select(events, "mutex_init")}
            require(len(ids) == 2, ids)
            require({e["lock_id"] for e in select(events, "lock_request")} == ids, "internal lock leaked")
            if warnings:
                require(set(warnings[0]["cycle"]) == ids and len(warnings[0]["cycle"]) == 3, warnings)
            require(all(e["monitoring_complete"] for e in events), events)
            balanced(events)
    print("PASS banking transparency: 5 safe + 5 unsafe real preload runs, balances/exit codes preserved, only account locks observed")
    _, events = fixture("recursive"); balanced(events)
    require(not select(events, "potential_lock_order_cycle"), events)
    require([e["held_depths"] for e in select(events, "lock_acquired")] == [[1], [2]], events)
    _, events = fixture("errorcheck"); balanced(events)
    require([e["return_code"] for e in select(events, "lock_failed")] == [errno.EDEADLK], events)
    require([e["return_code"] for e in select(events, "unlock_failed")] == [errno.EPERM], events)
    require(not select(events, "monitoring_incomplete"), events)
    print("PASS recursive depth, EDEADLK/EPERM preservation, failed-operation state, errno transparency")
    _, events = fixture("duplicate"); balanced(events)
    require(len(select(events, "potential_lock_order_cycle")) == 1, events)
    _, events = fixture("reuse"); balanced(events)
    inits = select(events, "mutex_init")
    require(len({e["lock_id"] for e in inits}) == 3 and len({e["mutex_address"] for e in inits}) == 1, inits)
    for mode in ("concurrent", "threads", "cancel"):
        _, events = fixture(mode); balanced(events)
        require(not select(events, "monitoring_incomplete") and not select(events, "potential_lock_order_cycle"), events)
        if mode != "cancel":
            require(len(select(events, "thread_start")) == 6 and len(select(events, "thread_end")) == 6, events)
            require(len({e["thread_instance"] for e in select(events, "thread_start")}) == 6, events)
    print("PASS duplicate suppression, address generations, concurrent JSON framing, separate TLS/thread exit, deferred cancellation")
    _, events = fixture("static"); balanced(events)
    require(len(select(events, "mutex_kind_unknown")) == 1 and not select(events, "potential_lock_order_cycle"), events)
    _, events = fixture("static-recursive")
    require(len(select(events, "potential_lock_order_cycle")) == 1 and select(events, "monitoring_incomplete"), events)
    for mode, reason in [("nodes", "graph_node_capacity"), ("registry", "identity_capacity"),
                         ("robust", "robust_or_interprocess_mutex_unsupported")]:
        _, events = fixture(mode)
        require(any(e.get("reason") == reason for e in select(events, "monitoring_incomplete")), events)
        require(not events[-1]["monitoring_complete"], events[-1])
    print("PASS explicit unknown-kind policy, unsupported robust warning, graph/identity capacity incompleteness")
    _, events = fixture("owner-dead")
    require([e["return_code"] for e in select(events, "lock_owner_dead")] == [errno.EOWNERDEAD], events)
    require(not select(events, "lock_failed"), events)
    _, events = fixture("trylock")
    require(not select(events, "lock_acquired") and not select(events, "lock_request"), events)
    require(any(e.get("reason") == "successful_unlock_not_tracked" for e in events), events)
    print("PASS unsupported trylock gap is explicit; EOWNERDEAD preserved and not mislabeled as ordinary lock failure")
    _, events = fixture("static", config={"RDP_ENABLED": "0"}); require(not events, events)
    _, events = fixture("static", config={"RDP_ENABLED": "invalid"})
    require(len(select(events, "monitoring_incomplete")) == 1 and not select(events, "lock_request"), events)
    for sink in ("stderr", "invalid", "closed", "readonly", "pipe", "broken", "full"):
        _, events = fixture("static", sink=sink)
        if sink in ("invalid", "closed", "readonly"):
            require(any(e.get("reason") == "invalid_event_fd_using_stderr" for e in events), events)
        elif sink in ("broken", "full"):
            require(len(select(events, "sink_error")) == 1, events)
        else:
            require(len(select(events, "lock_acquired")) == 1, events)
    print("PASS disablement, invalid configuration, dedicated FD/pipe/stderr, broken/full pipe preservation")
    for _ in range(2):
        run("banking_deadlock", ("--controlled-fixture",), deadlock=True, artifact="deadlock")
    check_interruption(signal.SIGINT)
    check_interruption(signal.SIGTERM)
    print("PASS Phase 4: real runtime integration checks; every child reaped and process group absent")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, KeyboardInterrupt) as error:
        raise SystemExit(f"FAIL: {error}") from error
