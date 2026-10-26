"""Optional unprivileged Linux Node bootstrap; never changes system packages."""
import hashlib
from pathlib import Path
import platform
import tarfile
import tempfile
import urllib.request

VERSION = 'v24.22.1'
ROOT = Path(__file__).resolve().parents[1]
target = ROOT / '.tools'
if platform.system() != 'Linux' or platform.machine() != 'x86_64':
    raise SystemExit('This optional bootstrap supports Linux x86_64. Install Node >=24.15 through your own package manager.')
name = f'node-{VERSION}-linux-x64'
url = f'https://nodejs.org/dist/{VERSION}/'
target.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory() as directory:
    archive = Path(directory) / (name + '.tar.xz')
    with urllib.request.urlopen(url + archive.name, timeout=60) as response:
        archive.write_bytes(response.read())
    with urllib.request.urlopen(url + 'SHASUMS256.txt', timeout=30) as response:
        expected = next(line.split()[0] for line in response.read().decode().splitlines() if line.endswith('  ' + archive.name))
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise SystemExit('Node archive checksum mismatch')
    with tarfile.open(archive) as bundle:
        bundle.extractall(target, filter='data')
link = target / 'node'
if not link.exists():
    link.symlink_to(name, target_is_directory=True)
print(f'Local Node: {link / "bin/node"}')
