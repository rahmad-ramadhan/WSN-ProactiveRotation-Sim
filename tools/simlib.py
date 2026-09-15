from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_binary() -> Path:
    """build/sim.exe first, then build/sim."""
    for name in ("sim.exe", "sim"):
        p = ROOT / "build" / name
        if p.is_file():
            return p
    sys.exit(
        "sim binary not found at build/sim.exe or build/sim - "
        "build first: cmake -S . -B build -G Ninja && cmake --build build"
    )


def run_sim(args: list[str], check: bool = False) -> subprocess.CompletedProcess:
    cmd = [str(find_binary()), *args]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if check and proc.returncode != 0:
        sys.exit(f"sim failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
    return proc

def read_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")


def read_summary(path: Path) -> dict[str, str]:
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    lines = [ln for ln in lines if ln != ""]
    if len(lines) != 2:
        sys.exit(f"{path}: expected header + one row, got {len(lines)} lines")
    import csv
    header = next(csv.reader([lines[0]]))
    row = next(csv.reader([lines[1]]))
    if len(header) != len(row):
        sys.exit(f"{path}: header has {len(header)} columns, row has {len(row)}")
    return dict(zip(header, row))


class Gate:

    def __init__(self, name: str):
        self.name = name
        self.failures = 0
        self.count = 0
        print(f"=== {name} ===")

    def check(self, ok: bool, label: str, detail: str = "") -> bool:
        self.count += 1
        tag = "[PASS]" if ok else "[FAIL]"
        if not ok:
            self.failures += 1
        line = f"{tag} {label}"
        if detail:
            line += f"  -- {detail}"
        print(line)
        return ok

    def note(self, text: str) -> None:
        print(f"       {text}")

    def finish(self) -> None:
        print(f"=== {self.name}: {self.count - self.failures}/{self.count} passed ===")
        sys.exit(1 if self.failures else 0)


def launch_progress(paths: list, extra: list | None = None) -> None:
    """Start progress.py detached. No-op without a display or when SIM_NO_PROGRESS is set."""
    import os
    import subprocess as _sp
    if os.environ.get("SIM_NO_PROGRESS"):
        return
    if os.name != "nt" and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        print("progress: no display; watch with  python tools/progress.py --text " + " ".join(str(p) for p in paths))
        return
    cmd = [sys.executable, str(ROOT / "tools" / "progress.py"), *[str(p) for p in paths], *(extra or [])]
    try:
        if os.name == "nt":
            flags = getattr(_sp, "DETACHED_PROCESS", 0) | getattr(_sp, "CREATE_NEW_PROCESS_GROUP", 0)
            _sp.Popen(cmd, creationflags=flags, stdin=_sp.DEVNULL, stdout=_sp.DEVNULL, stderr=_sp.DEVNULL, close_fds=True)
        else:
            _sp.Popen(cmd, start_new_session=True, stdin=_sp.DEVNULL, stdout=_sp.DEVNULL, stderr=_sp.DEVNULL)
        print("progress: window opened for " + ", ".join(str(p) for p in paths))
    except Exception as e:  # noqa: BLE001
        print(f"progress: could not open the window ({e}); run tools/progress.py by hand")
