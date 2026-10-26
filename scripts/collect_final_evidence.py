"""Preserve successful local final-gate artifacts and verify no demo servers remain."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import socket

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT / 'evidence/final'
    log = (output / 'check-final.log').read_text()
    if 'PASS final gate: all mandatory checks completed' not in log:
        raise SystemExit('Final gate has not completed successfully; preserve its failure log.')
    browser = ROOT / 'frontend/test-results'
    if json.loads((browser / '.last-run.json').read_text())['status'] != 'passed':
        raise SystemExit('Browser suite did not pass.')
    ports = {}
    for port in (8000, 5173):
        with socket.socket() as probe:
            ports[str(port)] = probe.connect_ex(('127.0.0.1', port)) != 0
    if not all(ports.values()):
        raise SystemExit('A demo port remains occupied; inspect ownership before cleanup.')
    remaining = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            executable = (proc / 'exe').resolve()
            command = (proc / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
            cwd = (proc / 'cwd').resolve()
        except (OSError, RuntimeError):
            continue
        child = executable.parent == ROOT / 'build' and executable.name.startswith(('banking_', 'test_runtime', 'benchmark'))
        server = cwd in (ROOT, ROOT / 'frontend') and ('uvicorn app.main:app' in command or 'node_modules/vite/bin/vite.js' in command)
        if child or server:
            remaining.append({'pid': int(proc.name), 'executable': str(executable)})
    if remaining:
        raise SystemExit(f'Owned workload/server processes remain: {remaining}')
    screenshots = output / 'screenshots'
    screenshots.mkdir(exist_ok=True)
    for path in browser.glob('*.png'):
        shutil.copyfile(path, screenshots / path.name)
    shutil.copyfile(browser / 'real-session-evidence.json', output / 'real-session-evidence.json')
    for path in (ROOT / 'build/phase6').glob('*.log'):
        shutil.copyfile(path, output / ('browser-' + path.name))
    before = json.loads((ROOT / 'evidence/phase7-source-before.json').read_text(encoding='utf-8-sig'))
    changed = []
    for row in before:
        relative = row['path'].replace('\\', '/')
        path = ROOT / relative
        digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None
        if digest != row['sha256'].lower():
            changed.append(relative)
    audit = {
        'verified_at_utc': datetime.now(timezone.utc).isoformat(),
        'loopback_ports_free': ports, 'remaining_owned_processes': remaining,
        'modified_preexisting_source_files': changed,
        'monitor_engine_unchanged': not any(p.startswith('monitor/') for p in changed),
        'screenshots': sorted(p.name for p in screenshots.glob('*.png')),
        'note': 'Source manifest predates Phase 7; new files are separately listed in final documentation. No commit or publication performed.',
    }
    (output / 'audit.json').write_text(json.dumps(audit, indent=2))
    files = []
    for path in sorted(output.rglob('*')):
        if path.is_file() and path.name != 'manifest.json':
            files.append({'path': str(path.relative_to(output)), 'bytes': path.stat().st_size,
                          'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (output / 'manifest.json').write_text(json.dumps(files, indent=2))
    print(json.dumps(audit, indent=2))


if __name__ == '__main__':
    main()
