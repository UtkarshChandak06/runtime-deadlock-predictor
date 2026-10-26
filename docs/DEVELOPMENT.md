# Development with Windows, WSL2, VS Code, and Codex

## This machine

The controlling terminal is native Windows PowerShell. Linux build commands must
run in Ubuntu/WSL, not in that PowerShell terminal. Ubuntu is already installed
and uses WSL2; no WSL installation or configuration change was needed.

VS Code is installed. Its installed extension list did not include Microsoft's
WSL extension (`ms-vscode-remote.remote-wsl`). Install that extension manually
from VS Code Extensions, then use **WSL: Connect to WSL using Distro**, select
Ubuntu, and open this folder:

```text
/mnt/c/Users/utkar/OneDrive/Desktop/OS PROJECT/deadlock-sentinel
```

Check that the lower-left remote indicator says **WSL: Ubuntu**. Open a new
integrated terminal and run `uname -r` to confirm Linux. The VS Code GUI/remote
connection has not been interactively verified by the Phase 1 checks.

Alternatively, open Ubuntu from Windows Terminal and run:

```bash
cd "/mnt/c/Users/utkar/OneDrive/Desktop/OS PROJECT/deadlock-sentinel"
code .
```

The first remote connection may download the VS Code server. For larger future
builds, keeping a checkout under your Linux home can improve filesystem speed.
The present workspace with spaces was tested as-is; always quote its path.

## Build-edit-test loop

```bash
make help
make clean
make
make run
bash scripts/check_phase1.sh
make banking-safe
make banking-unsafe
make check-phase2
make analysis-demo
make check-phase3
make monitor-build
make monitor-safe
make monitor-unsafe
make check-phase4
git status --short
```

GCC converts the C source to a native Linux executable. `-std=c11` selects C11;
`-Wall -Wextra -Wpedantic` enable useful diagnostics; `-g` adds debugging data;
`-pthread` supplies the compiler/linker settings for POSIX threads. Make rebuilds
when the source changes. The C-only phases need no framework. The full application also needs the local backend environment and frontend dependencies described in README.md.

Keep text files in LF format; `.gitattributes` makes Git normalize line endings.
Make recipes must begin with a tab. Invoke the checker with `bash`, so it does not
depend on executable permission handling on Windows-mounted filesystems.

Ask Codex for one phase at a time, have it inspect the current files, and ask for
actual compiler/test output. Run Linux commands through WSL and distinguish
implemented features from the roadmap. Never treat sample output as test evidence.

Git was initialized locally. No commit, remote, push, or publication is required
to build. Review `git diff` and `git status` before making your own commit; do not
add secrets. `.env.example` and `.env.template` can hold non-secret templates.

## Setup on another machine (manual only)

If WSL is missing, open administrator PowerShell and run `wsl --install -d Ubuntu`.
Restart if requested, launch Ubuntu, and create the Linux username/password. Check
`wsl --list --verbose`; if Ubuntu shows version 1, use
`wsl --set-version Ubuntu 2`. These are user-run setup instructions; this project
does not perform privileged changes.

If Linux build tools are missing, review and run these inside Ubuntu:

```bash
sudo apt update
sudo apt install build-essential git python3
```

Python 3 runs the Phase 2 standard-library harness. Linux-native Node/npm is required for the implemented frontend; a Windows npm exposed through WSL's PATH does not
establish a Linux Node environment. Do not install FastAPI or React for Phases 1–4.

For a separate controlled deadlock demonstration, build with
`make banking-deadlock-build`, then run
`python3 tests/test_banking.py --deadlock-only`. Never invoke the deadlock binary
directly. The parent bounds execution to 2.5 seconds and cleans up on timeout,
Ctrl+C, and SIGTERM. The evidence check requires readable Linux `/proc` task
`wchan` files; restricted systems may report an inconclusive failure.

Phase 3 builds a static C library with GCC and `ar` (from binutils, normally
included with build-essential). Use `make check-phase3-sanitize` for optional
AddressSanitizer/UndefinedBehaviorSanitizer checks and `make check-phase3-tsan`
for ThreadSanitizer. These targets build separate executables so their flags
cannot accidentally reuse ordinary object files. Phase 3 does not use preload
environment variables or change the banking launch commands.

Phase 4 adds optional preload launch targets, leaving existing runs unchanged.
Use `make monitor-deadlock` for the monitored bounded fixture. Use
`make check-phase4-ubsan` for the tested runtime sanitizer combination; do not
assume ThreadSanitizer/AddressSanitizer can be combined with arbitrary preloads.
See RUNTIME_MONITOR.md for relative-path launches and dedicated JSONL descriptors.
