"""Linux-only bounded supervisor. No shell and no request-controlled executable."""
import asyncio
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import signal
import tempfile


@dataclass(frozen=True)
class Application:
    application_id: str
    display_name: str
    description: str
    scenario: str
    deadlock: bool = False

    @property
    def executable(self):
        return "build/" + self.application_id


CATALOG = {
    "banking_safe": Application("banking_safe", "Safe banking", "Consistent account-ID ordering", "safe"),
    "banking_unsafe": Application("banking_unsafe", "Unsafe lock ordering", "Sequential opposite orders; completes normally", "potential_cycle"),
    "banking_deadlock": Application("banking_deadlock", "Controlled deadlock", "Barrier fixture; always bounded to 2.5 seconds", "controlled_deadlock", True),
}


def dependencies(root, application):
    executable = (root / application.executable).resolve()
    library = (root / "build/libdeadlock_monitor.so").resolve()
    # Trusted repository paths, additionally constrained against escaping symlinks.
    if not executable.is_relative_to(root.resolve()) or not library.is_relative_to(root.resolve()):
        raise FileNotFoundError("approved_artifact_outside_repository")
    if not executable.is_file() or not os.access(executable, os.X_OK) or not library.is_file():
        raise FileNotFoundError("build_missing_run_make_monitor_build_banking_build")
    if library.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("monitor_library_size_limit")
    return executable, library


def kill_group(pid):
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def group_absent(pid):
    try:
        os.killpg(pid, 0)
    except ProcessLookupError:
        return True
    return False


def wait_channels(pid):
    try:
        return [(task / "wchan").read_text().strip() for task in Path(f"/proc/{pid}/task").iterdir()]
    except OSError:
        return []


@dataclass
class RunResult:
    exit_code: int
    timed_out: bool
    stopped: bool
    cleanup_verified: bool
    wait_channels: list[str]
    drain_complete: bool


async def supervise(settings, application, stop, on_started, on_events, on_output):
    executable, library = dependencies(settings.root, application)
    process = None
    transport = None
    read_file = None
    write_fd = None
    drains = []
    waiters = []
    # The fixed /tmp parent avoids TMPDIR values containing loader separators.
    with tempfile.TemporaryDirectory(prefix="deadlock-sentinel-", dir="/tmp") as directory:
        copied = Path(directory) / "monitor.so"
        shutil.copyfile(library, copied)
        if hashlib.sha256(library.read_bytes()).digest() != hashlib.sha256(copied.read_bytes()).digest():
            raise RuntimeError("monitor_copy_hash_mismatch")
        os.chmod(copied, 0o500)
        read_fd, write_fd = os.pipe()
        read_file = os.fdopen(read_fd, "rb", buffering=0)
        reader = asyncio.StreamReader(limit=settings.max_line_bytes * 2)
        environment = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "RDP_ENABLED": "1",
                       "RDP_EVENT_FD": str(write_fd), "LD_PRELOAD": str(copied)}
        timed_out = stopped = False
        channels = []
        drain_complete = True
        try:
            transport, _ = await asyncio.get_running_loop().connect_read_pipe(
                lambda: asyncio.StreamReaderProtocol(reader), read_file)
            spawn = asyncio.create_task(asyncio.create_subprocess_exec(
                str(executable), *(["--controlled-fixture"] if application.deadlock else []),
                cwd=settings.root, env=environment, pass_fds=(write_fd,), start_new_session=True,
                stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE))
            # Cancellation during spawn must still collect the process handle for cleanup.
            try:
                process = await asyncio.shield(spawn)
            except asyncio.CancelledError:
                process = await spawn
                raise
            finally:
                os.close(write_fd); write_fd = None
            on_started(process.pid)

            async def drain(source, callback):
                while chunk := await source.read(4096):
                    callback(chunk)

            drains = [asyncio.create_task(drain(reader, on_events)),
                      asyncio.create_task(drain(process.stdout, lambda data: on_output("stdout", data))),
                      asyncio.create_task(drain(process.stderr, lambda data: on_output("stderr", data)))]
            process_wait = asyncio.create_task(process.wait())
            stop_wait = asyncio.create_task(stop.wait())
            waiters = [process_wait, stop_wait]
            timeout = settings.deadlock_timeout if application.deadlock else settings.normal_timeout
            deadline = asyncio.get_running_loop().time() + timeout
            pending_drains = set(drains)
            while not process_wait.done() and not stop_wait.done():
                remaining = deadline - asyncio.get_running_loop().time()
                if remaining <= 0:
                    break
                done, _ = await asyncio.wait(waiters + list(pending_drains), timeout=remaining,
                                             return_when=asyncio.FIRST_COMPLETED)
                if not done:
                    break
                for task in done & pending_drains:
                    task.result()
                    pending_drains.remove(task)
            if not process_wait.done():
                stopped = stop.is_set()
                timed_out = not stopped
                if timed_out and application.deadlock:
                    channels = wait_channels(process.pid)
                kill_group(process.pid)
            await process_wait
            # Also clean descendants that might keep inherited output FDs open.
            kill_group(process.pid)
            try:
                await asyncio.wait_for(asyncio.gather(*drains), timeout=2)
            except TimeoutError:
                drain_complete = False
            return RunResult(process.returncode, timed_out, stopped,
                             group_absent(process.pid) and not Path(f"/proc/{process.pid}").exists(), channels, drain_complete)
        finally:
            if write_fd is not None:
                os.close(write_fd)
            if process is not None:
                kill_group(process.pid)
                await process.wait()
            for task in waiters + drains:
                if not task.done():
                    task.cancel()
            if waiters or drains:
                await asyncio.gather(*(waiters + drains), return_exceptions=True)
            if transport is not None:
                transport.close()
            elif read_file is not None:
                read_file.close()
