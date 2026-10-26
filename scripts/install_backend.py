"""Install only into backend/.venv, including Ubuntu without ensurepip."""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import venv

ROOT = Path(__file__).resolve().parents[1]
ENV = ROOT / "backend" / ".venv"
PYTHON = ENV / "bin" / "python"
PIP_VERSION = "26.2.2"


def main():
    venv.EnvBuilder(with_pip=False, symlinks=True).create(ENV)
    if subprocess.run([str(PYTHON), "-m", "pip", "--version"], capture_output=True).returncode:
        with urllib.request.urlopen(f"https://pypi.org/pypi/pip/{PIP_VERSION}/json", timeout=30) as response:
            metadata = json.load(response)
        wheel = next(item for item in metadata["urls"] if item["filename"].endswith("py3-none-any.whl"))
        with urllib.request.urlopen(wheel["url"], timeout=30) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != wheel["digests"]["sha256"]:
            raise SystemExit("pip wheel checksum mismatch")
        archive = ENV / wheel["filename"]
        archive.write_bytes(data)
        bootstrap = "import sys,runpy;sys.path.insert(0,sys.argv.pop(1));runpy.run_module('pip',run_name='__main__')"
        subprocess.run([str(PYTHON), "-c", bootstrap, str(archive), "install", str(archive)], check=True)
        archive.unlink()
    subprocess.run([str(PYTHON), "-m", "pip", "install", "-r", str(ROOT / "backend" / "requirements.txt")], check=True)
    subprocess.run([str(PYTHON), "-m", "pip", "check"], check=True)


if __name__ == "__main__":
    main()
