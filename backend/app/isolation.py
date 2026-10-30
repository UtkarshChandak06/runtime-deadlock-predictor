"""Fail-closed Linux runner: Bubblewrap namespaces + systemd user cgroup scopes.

No source, compiler subprocess, or user binary is ever executed outside this
boundary. Only generated sources and individual build artifacts are exposed.
"""
import asyncio
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import time
import uuid
import json

COMPILER = ['/usr/bin/gcc', '-std=c11', '-Wall', '-Wextra', '-Wpedantic',
            '-g', '-O0', '-fno-omit-frame-pointer', '-pthread', '-I/src']
SETUP = ('Install bubblewrap and gcc in Ubuntu, enable the systemd user manager '
         'with delegated cgroup v2 memory/pids/cpu controllers, then restart the backend. '
         'Custom compilation and execution never fall back to the host.')


@dataclass
class Result:
    exit_code: int
    timed_out: bool
    stopped: bool
    cleanup_verified: bool
    wait_channels: list
    drain_complete: bool
    duration: float
    limit_hit: str | None = None
    target_returncode: int | None = None


class Isolation:
    def __init__(self):
        self.available = False
        self.reason = 'Isolation has not been checked. ' + SETUP
        self.units = set()
        self.version = ''

    def base(self, unit, seconds):
        return ['/usr/bin/systemd-run', '--user', '--scope', '--quiet', '--collect',
                '--unit=' + unit, '-p', 'MemoryMax=268435456', '-p', 'MemorySwapMax=0',
                '-p', 'TasksMax=32', '-p', 'CPUQuota=100%', '-p', 'RuntimeMaxSec=' + str(seconds + 3),
                '/usr/bin/bwrap', '--unshare-all', '--unshare-user', '--disable-userns',
                '--die-with-parent', '--new-session', '--cap-drop', 'ALL',
                '--uid', '1000', '--gid', '1000', '--clearenv',
                '--setenv', 'PATH', '/usr/bin', '--setenv', 'LANG', 'C', '--setenv', 'HOME', '/tmp',
                '--setenv', 'TMPDIR', '/tmp',
                '--ro-bind', '/usr', '/usr', '--symlink', 'usr/bin', '/bin',
                '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib64', '/lib64',
                '--proc', '/proc', '--dev', '/dev', '--size', '33554432', '--tmpfs', '/tmp',
                '--size', '16777216', '--tmpfs', '/out']

    async def check(self):
        if os.name != 'posix' or os.getuid() == 0 or any(not Path(p).is_file() for p in
                ['/usr/bin/systemd-run', '/usr/bin/systemctl', '/usr/bin/bwrap', '/usr/bin/gcc', '/usr/bin/prlimit']):
            self.reason = 'An unprivileged Linux user and required runner tools are needed. ' + SETUP
            return self.status()
        out = bytearray()
        try:
            # This also checks the FD channel, namespace PID, no-new-privileges,
            # dropped capabilities and the actual applied cgroup constraints.
            async def validate_group(unit):
                required = (b'MemoryMax=268435456', b'MemorySwapMax=0', b'TasksMax=32', b'CPUQuotaPerSecUSec=1s')
                for _ in range(10):
                    proc = await asyncio.create_subprocess_exec('/usr/bin/systemctl', '--user', 'show', unit+'.scope',
                        '-p', 'MemoryMax', '-p', 'MemorySwapMax', '-p', 'TasksMax', '-p', 'CPUQuotaPerSecUSec','-p','ControlGroup',
                        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
                    raw, error = await asyncio.wait_for(proc.communicate(), 3)
                    if proc.returncode==0 and all(x in raw for x in required):
                        props=dict(line.split('=',1) for line in raw.decode().splitlines() if '=' in line)
                        group=(Path('/sys/fs/cgroup')/props['ControlGroup'].lstrip('/')).resolve()
                        if not group.is_relative_to(Path('/sys/fs/cgroup')):raise RuntimeError('invalid cgroup path')
                        if ((group/'memory.max').read_text().strip()!='268435456' or
                            (group/'memory.swap.max').read_text().strip()!='0' or
                            (group/'pids.max').read_text().strip()!='32'):
                            raise RuntimeError('kernel resource controls not applied')
                        quota,period=(group/'cpu.max').read_text().split()
                        if quota=='max' or int(quota)>int(period):raise RuntimeError('kernel CPU quota not applied')
                        return
                    await asyncio.sleep(.1)
                raise RuntimeError('cgroup controllers not applied: '+(raw+error).decode(errors='replace')[:300])
            result = await self.execute([], ['/usr/bin/bash', '-c',
                'cat /proc/self/status; /usr/bin/gcc -dumpfullversion; printf channel >&$1; sleep 2',
                'probe', '{channel}'], 4, asyncio.Event(), lambda pid: None,
                lambda data: out.extend(data), lambda name, data: out.extend(data),
                probe=validate_group)
            if (result.exit_code or not result.cleanup_verified or b'channel' not in out or
                    b'NoNewPrivs:\t1' not in out or b'CapEff:\t0000000000000000' not in out):
                raise RuntimeError(out.decode(errors='replace')[:500])
            self.version = out.decode(errors='replace').splitlines()[-1].removesuffix('channel')
            self.available = True
            self.reason = ''
        except Exception as error:
            self.available = False
            self.reason = 'Isolation probe failed: ' + str(error)[:600] + ' Runner: ' + out.decode(errors='replace')[-500:] + '. ' + SETUP
        return self.status()

    def status(self):
        return dict(available=self.available, reason=self.reason,
            provider='bubblewrap-systemd-cgroup-v2',
            limits=dict(memory_bytes=268435456, processes=32, cpu_quota='1 CPU',
                        scratch_bytes=50331648, file_bytes=8388608,
                        output_bytes=1048576, event_bytes=2097152, run_seconds=[1,30], build_seconds=20),
            source_location='source location unavailable',
            trust='Same-process instrumentation is not tamper-proof. User code can suppress or forge monitor records.')

    async def cleanup(self, unit):
        proc = await asyncio.create_subprocess_exec('/usr/bin/systemctl', '--user', 'stop', unit+'.scope',
            stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        try:
            await asyncio.wait_for(proc.wait(), 5)
        except BaseException:
            if proc.returncode is None:proc.kill()
            await proc.wait()
            raise
        proc = await asyncio.create_subprocess_exec('/usr/bin/systemctl', '--user', 'show', unit+'.scope',
            '-p', 'ActiveState', '--value', stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
        try:
            raw,_ = await asyncio.wait_for(proc.communicate(), 3)
        except BaseException:
            if proc.returncode is None:proc.kill()
            await proc.wait()
            raise
        return raw.strip() in {b'', b'inactive', b'failed'}

    async def execute(self, mounts, command, seconds, stop, started, channel, output,
                      stdin=b'', probe=None, channel_limit=2097152, unit_started=None):
        unit = 'deadlock-sentinel-' + uuid.uuid4().hex
        self.units.add(unit)
        begin = time.monotonic()
        proc = None; tasks = []; transport = None; read_file = None
        status_transport=None;status_file=None;status_write=None;status_data=bytearray()
        read_fd, write_fd = os.pipe()
        timed_out = stopped = False; limit_hit = None; clean = False
        try:
            if unit_started:unit_started(unit)
            reader = asyncio.StreamReader()
            read_file = os.fdopen(read_fd, 'rb', buffering=0)
            transport,_ = await asyncio.get_running_loop().connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), read_file)
            status_reader=None
            if '{status}' in command:
                status_read,status_write=os.pipe()
                status_file=os.fdopen(status_read,'rb',buffering=0)
                status_reader=asyncio.StreamReader()
                status_transport,_=await asyncio.get_running_loop().connect_read_pipe(lambda:asyncio.StreamReaderProtocol(status_reader),status_file)
            cmd = self.base(unit, seconds) + mounts + ['--remount-ro', '/dev', '--remount-ro', '/', '--chdir', '/tmp', '--',
                  '/usr/bin/prlimit', '--cpu='+str(seconds+1), '--fsize=8388608', '--nofile=64', '--core=0', '--']
            cmd += [x.replace('{channel}',str(write_fd)).replace('{status}',str(status_write)) for x in command]
            # Fixed clean parent environment; only the user-manager bus address
            # remains outside the sandbox and is cleared by Bubblewrap.
            env = {'PATH':'/usr/bin:/bin', 'LANG':'C', 'HOME':str(Path.home()),
                   'XDG_RUNTIME_DIR':os.environ.get('XDG_RUNTIME_DIR', '/run/user/'+str(os.getuid()))}
            spawn = asyncio.create_task(asyncio.create_subprocess_exec(*cmd, stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
                pass_fds=tuple(fd for fd in (write_fd,status_write) if fd is not None),env=env))
            try: proc = await asyncio.shield(spawn)
            except asyncio.CancelledError:
                proc = await spawn
                raise
            os.close(write_fd); write_fd = None
            if status_write is not None:os.close(status_write);status_write=None
            started(proc.pid)
            budget = {'output':0, 'channel':0}
            exceeded = asyncio.Event()
            async def drain(source, kind, callback, bound):
                nonlocal limit_hit
                while data := await source.read(4096):
                    budget[kind] += len(data)
                    if budget[kind] > bound:
                        limit_hit = kind + '_limit'; exceeded.set()
                    else: callback(data)
            async def feed():
                try:
                    proc.stdin.write(stdin); await proc.stdin.drain()
                except (BrokenPipeError, ConnectionResetError): pass
                finally: proc.stdin.close()
            tasks = [asyncio.create_task(drain(reader,'channel',channel,channel_limit)),
                     asyncio.create_task(drain(proc.stdout,'output',lambda x:output('stdout',x),1048576)),
                     asyncio.create_task(drain(proc.stderr,'output',lambda x:output('stderr',x),1048576)),
                     asyncio.create_task(feed())]
            if status_reader:
                budget['status']=0
                tasks.append(asyncio.create_task(drain(status_reader,'status',status_data.extend,512)))
            drain_count=len(tasks)
            if probe:
                await asyncio.sleep(.2)
                await probe(unit)
            wait = asyncio.create_task(proc.wait()); halted = asyncio.create_task(stop.wait())
            capped = asyncio.create_task(exceeded.wait()); tasks += [wait,halted,capped]
            done,_ = await asyncio.wait([wait,halted,capped],timeout=seconds,return_when=asyncio.FIRST_COMPLETED)
            if wait not in done:
                stopped = stop.is_set()
                timed_out = not stopped and not exceeded.is_set()
            clean = await self.cleanup(unit)
            await asyncio.wait_for(wait, 5)
            await asyncio.wait_for(asyncio.gather(*tasks[:drain_count]), 3)
            target_returncode=None
            if status_data:
                value=json.loads(status_data)
                if type(value.get('returncode')) is int and value.get('namespace_pid')==3:
                    target_returncode=value['returncode']
            if status_reader and target_returncode is None and not timed_out and not stopped and not limit_hit:
                limit_hit='target_status_unavailable'
            return Result(proc.returncode,timed_out,stopped,clean,[],not limit_hit,time.monotonic()-begin,limit_hit,target_returncode)
        finally:
            if write_fd is not None: os.close(write_fd)
            if status_write is not None:os.close(status_write)
            try:
                try:
                    await self.cleanup(unit)
                finally:
                    if proc and proc.returncode is None:
                        proc.kill(); await proc.wait()
            finally:
                for task in tasks:
                    if not task.done():task.cancel()
                if tasks: await asyncio.gather(*tasks,return_exceptions=True)
                if transport: transport.close()
                elif read_file: read_file.close()
                else:os.close(read_fd)
                if status_transport:status_transport.close()
                elif status_file:status_file.close()
                self.units.discard(unit)
