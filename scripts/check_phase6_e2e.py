"""Own real local servers and run the browser suite; always stop owned servers."""
import os
from pathlib import Path
import signal
import socket
import subprocess
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
for port in (8000, 5173):
    with socket.socket() as probe:
        if probe.connect_ex(('127.0.0.1', port)) == 0:
            raise SystemExit(f'Port {port} is in use. Stop your own dev server before this managed test; it will not be terminated automatically.')
node = ROOT / '.tools/node/bin/node'
if not node.exists():
    import shutil
    found = shutil.which('node')
    if not found:
        raise SystemExit('Linux Node required; see frontend/README.md')
    node = Path(found)
logs = ROOT / 'build/phase6'
logs.mkdir(parents=True, exist_ok=True)
processes = []
files = []
try:
    for name, command, cwd in [
        ('backend', [str(ROOT/'backend/.venv/bin/python'), '-m', 'uvicorn', 'app.main:app', '--app-dir', 'backend', '--host', '127.0.0.1', '--port', '8000', '--no-proxy-headers', '--ws', 'websockets-sansio', '--timeout-graceful-shutdown', '5'], ROOT),
        ('frontend', [str(node), 'node_modules/vite/bin/vite.js'], ROOT/'frontend')]:
        log = (logs/(name+'.log')).open('w'); files.append(log)
        processes.append(subprocess.Popen(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True))
    for url in ('http://127.0.0.1:8000/api/v1/health','http://127.0.0.1:5173'):
        for attempt in range(120):
            try:
                with urllib.request.urlopen(url, timeout=1) as response:
                    if response.status == 200:
                        break
            except OSError:
                time.sleep(.25)
        else:
            raise RuntimeError('Local server did not become ready; inspect build/phase6 logs')
    # WSL may use the already-installed Windows Chrome without privileged Linux packages.
    windows_node = Path('/mnt/c/Program Files/nodejs/node.exe')
    browser_node = windows_node if windows_node.exists() else node
    result = subprocess.run([str(browser_node), 'node_modules/@playwright/test/cli.js', 'test'], cwd=ROOT/'frontend', timeout=180)
    raise SystemExit(result.returncode)
finally:
    for process in reversed(processes):
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
    for process in reversed(processes):
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL); process.wait()
    for file in files:
        file.close()
