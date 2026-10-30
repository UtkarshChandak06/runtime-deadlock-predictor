"""Trusted in-sandbox parent. Its private status FD is not inherited by user code."""
import ctypes
import json
import os
import subprocess
import sys

event_fd, status_fd = int(sys.argv[1]), int(sys.argv[2])
# Prevent another same-UID process from reading this parent's file descriptors
# through procfs/ptrace. The application still controls its own event channel.
if ctypes.CDLL(None).prctl(4, 0, 0, 0, 0) != 0:  # PR_SET_DUMPABLE
    raise SystemExit(125)
environment = {'PATH':'/usr/bin', 'LANG':'C', 'HOME':'/tmp', 'TMPDIR':'/tmp',
               'LD_PRELOAD':'/monitor.so', 'RDP_ENABLED':'1', 'RDP_EVENT_FD':str(event_fd)}
child = subprocess.Popen(['/program', *sys.argv[3:]], env=environment,
                         pass_fds=(event_fd,), stdin=0, stdout=1, stderr=2)
result = child.wait()
os.write(status_fd, json.dumps({'returncode':result, 'namespace_pid':child.pid}).encode()+b'\n')
os.close(event_fd)
os.close(status_fd)
