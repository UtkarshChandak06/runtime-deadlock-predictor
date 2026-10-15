#!/usr/bin/env python3
"""Reproducible standalone sanitizers, never preload ASan/TSan into the app."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
for sanitizer in ['thread', 'address,undefined']:
    for test in ['graph', 'graph_limits', 'tracker', 'log', 'log_cancel']:
        output = root / 'build' / ('sanitize_' + sanitizer.replace(',','_') + '_' + test)
        cmd = ['gcc', '-D_GNU_SOURCE', '-Iinclude', '-Wall', '-Wextra', '-O1', '-g',
               '-fsanitize=' + sanitizer, '-pthread', '-o', str(output),
               'tests/unit/test_' + test + '.c', 'src/graph.c', 'src/tracker.c', 'src/log.c', 'src/report.c']
        if test in ('log', 'log_cancel'): cmd += ['-Wl,--wrap=write']
        print('COMMAND:', ' '.join(cmd), flush=True)
        subprocess.run(cmd, cwd=root, check=True, timeout=30)
        print('COMMAND: timeout 30s', output, flush=True)
        subprocess.run(['timeout', '30s', str(output)], cwd=root, check=True, timeout=35)
        print('PASS', sanitizer, test, flush=True)

