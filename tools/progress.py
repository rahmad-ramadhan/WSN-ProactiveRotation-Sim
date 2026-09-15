"""Live progress for a campaign or sensitivity matrix. Reads only the filesystem.

A run starts when config_in.json appears and is done when summary.csv appears.
Totals come from plan.json, else --arms/--seeds/--rho, else campaign.json.

Usage:
  python tools/progress.py out/sensitivity
  python tools/progress.py out/campaign --arms static,radpr --seeds 1..30
  python tools/progress.py out/sensitivity --text
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import statistics
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

CONFIG_IN = "config_in.json"
SUMMARY = "summary.csv"
PLAN = "plan.json"
MANIFEST = "campaign.json"
RECENT_RUNS_FOR_RATE = 20      # median over this many recent runs
STALE_AFTER_S = 600            # seconds before an unfinished run shows as stalled


@dataclass
class Run:
    run_id: str
    started: float                 # mtime of config_in.json
    finished: float | None         # mtime of summary.csv; None while running

    @property
    def duration(self) -> float | None:
        return None if self.finished is None else max(0.0, self.finished - self.started)


@dataclass
class Batch:
    name: str
    path: Path
    expected: int | None
    expected_ids: set[str] | None
    runs: list[Run] = field(default_factory=list)
    failed: int = 0

    @property
    def done(self) -> list[Run]:
        return [r for r in self.runs if r.finished is not None]

    @property
    def running(self) -> list[Run]:
        return [r for r in self.runs if r.finished is None]

    @property
    def total(self) -> int | None:
        if self.expected is not None:
            return max(self.expected, len(self.runs))
        return None

    @property
    def remaining(self) -> int | None:
        return None if self.total is None else max(0, self.total - len(self.done))


def parse_seeds(text: str) -> list[int]:
    text = text.strip()
    if ".." in text:
        a, b = text.split("..", 1)
        return list(range(int(a), int(b) + 1))
    return [int(s) for s in text.split(",") if s.strip()]


def is_run_dir(p: Path) -> bool:
    return p.is_dir() and (p / CONFIG_IN).is_file()


def scan_runs(batch_dir: Path) -> list[Run]:
    runs: list[Run] = []
    try:
        children = list(batch_dir.iterdir())
    except OSError:
        return runs
    for d in children:
        if not is_run_dir(d):
            continue
        try:
            started = (d / CONFIG_IN).stat().st_mtime
        except OSError:
            continue
        s = d / SUMMARY
        finished: float | None
        try:
            finished = s.stat().st_mtime if s.is_file() else None
        except OSError:
            finished = None
        runs.append(Run(d.name, started, finished))
    runs.sort(key=lambda r: r.started)
    return runs


def read_json(p: Path) -> dict | None:
    try:
        with open(p, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def expected_from_manifest(batch_dir: Path) -> tuple[int | None, int]:
    man = read_json(batch_dir / MANIFEST)
    if not man:
        return None, 0
    return man.get("runs_total"), int(man.get("runs_failed", 0) or 0)


def discover(root: Path, cli_arms: list[str] | None, cli_seeds: list[int] | None, cli_rho: int | None) -> list[Batch]:
    plan = read_json(root / PLAN)
    if plan and "cells" in plan:
        seeds = plan.get("seeds", [])
        rho = plan.get("rho")
        batches = []
        for cell in plan["cells"]:
            arms = cell.get("arms", [])
            ids = {f"{a}_rho{rho}_s{s}" for a in arms for s in seeds} if rho is not None else None
            b = Batch(name=f"{cell.get('id', '?')}  {cell.get('dir', '')}", path=root / cell.get("dir", ""),
                      expected=len(arms) * len(seeds), expected_ids=ids)
            batches.append(b)
        return batches

    if is_batch_dir(root):
        return [make_plain_batch(root, root.name, cli_arms, cli_seeds, cli_rho)]

    batches = []
    try:
        for child in sorted(root.iterdir()):
            if is_batch_dir(child):
                batches.append(make_plain_batch(child, child.name, cli_arms, cli_seeds, cli_rho))
    except OSError:
        pass
    if not batches:
        batches.append(make_plain_batch(root, root.name, cli_arms, cli_seeds, cli_rho))
    return batches


def is_batch_dir(p: Path) -> bool:
    if not p.is_dir():
        return False
    try:
        return any(is_run_dir(c) for c in p.iterdir())
    except OSError:
        return False


def make_plain_batch(path: Path, name: str, arms, seeds, rho) -> Batch:
    expected = None
    ids = None
    if arms and seeds:
        expected = len(arms) * len(seeds)
        if rho is not None:
            ids = {f"{a}_rho{rho}_s{s}" for a in arms for s in seeds}
    failed = 0
    if expected is None:
        expected, failed = expected_from_manifest(path)
    return Batch(name=name, path=path, expected=expected, expected_ids=ids, failed=failed)


def refresh(batches: list[Batch]) -> None:
    for b in batches:
        b.runs = scan_runs(b.path)
        if b.expected_ids is None and b.expected is None:
            b.expected, b.failed = expected_from_manifest(b.path)
        elif (b.path / MANIFEST).is_file():
            _, b.failed = expected_from_manifest(b.path)


def median_duration(runs: list[Run]) -> float | None:
    ds = [d for _, d in sorted((r.finished, r.duration) for r in runs if r.duration is not None)]
    if not ds:
        return None
    return statistics.median(ds[-RECENT_RUNS_FOR_RATE:])


def fmt_seconds(s: float | None) -> str:
    if s is None:
        return "-"
    s = int(round(s))
    h, rem = divmod(s, 3600)
    m, sec = divmod(rem, 60)
    if h:
        return f"{h}h {m:02d}m"
    if m:
        return f"{m}m {sec:02d}s"
    return f"{sec}s"


def fmt_clock(ts: float | None) -> str:
    if ts is None:
        return "-"
    return _dt.datetime.fromtimestamp(ts).strftime("%H:%M")


@dataclass
class Row:
    name: str
    total: str
    done: str
    running: str
    remaining: str
    percent: str
    per_run: str
    elapsed: str
    eta: str
    finish: str
    state: str


def build_rows(batches: list[Batch], now: float) -> tuple[list[Row], dict]:
    all_done = [r for b in batches for r in b.done]
    global_rate = median_duration(all_done)
    rows: list[Row] = []
    total_remaining = 0
    total_expected = 0
    total_done = 0
    total_unknown = False
    eta_sum = 0.0
    eta_known = True
    any_running = False

    for b in batches:
        done = b.done
        running = b.running
        rate = median_duration(done) if len(done) >= 3 else global_rate
        rem = b.remaining
        total_done += len(done)
        if b.total is None:
            total_unknown = True
        else:
            total_expected += b.total
            total_remaining += rem or 0

        in_flight = 0.0
        state = ""
        if running:
            any_running = True
            cur = running[-1]
            in_flight = now - cur.started
            state = f"running {cur.run_id} ({fmt_seconds(in_flight)})"
            if in_flight > STALE_AFTER_S:
                state = f"STALLED? {cur.run_id} started {fmt_seconds(in_flight)} ago"
        elif rem == 0 and b.total:
            state = "complete" if not b.failed else f"complete, {b.failed} FAILED"
        elif not done:
            state = "not started"
        else:
            state = "idle"

        eta: float | None = None
        if rem is not None and rate is not None:
            eta = max(0.0, rem * rate - in_flight)
            eta_sum += eta
        elif rem not in (None, 0):
            eta_known = False

        elapsed = None
        if done or running:
            first = min(r.started for r in b.runs)
            last = max((r.finished for r in done), default=now) if not running else now
            elapsed = last - first

        pct = "-" if b.total in (None, 0) else f"{100.0 * len(done) / b.total:5.1f}%"
        rows.append(Row(
            name=b.name,
            total="?" if b.total is None else str(b.total),
            done=str(len(done)),
            running=str(len(running)),
            remaining="?" if rem is None else str(rem),
            percent=pct,
            per_run=fmt_seconds(rate),
            elapsed=fmt_seconds(elapsed),
            eta="-" if eta is None else (fmt_seconds(eta) if rem else "done"),
            finish="-" if eta is None or not rem else fmt_clock(now + eta),
            state=state,
        ))

    overall = {
        "done": total_done,
        "expected": None if total_unknown else total_expected,
        "remaining": None if total_unknown else total_remaining,
        "eta": eta_sum if eta_known and not total_unknown else None,
        "rate": global_rate,
        "running": any_running,
    }
    return rows, overall


def overall_text(o: dict, now: float) -> str:
    if o["expected"] is None:
        head = f"{o['done']} runs done, total unknown"
    else:
        pct = 100.0 * o["done"] / o["expected"] if o["expected"] else 0.0
        head = f"{o['done']} / {o['expected']} runs  ({pct:.1f}%)  {o['remaining']} remaining"
    if o["eta"] is not None and o["remaining"]:
        head += f"   -   about {fmt_seconds(o['eta'])} left, finishing ~{fmt_clock(now + o['eta'])}"
    elif o["expected"] is not None and o["remaining"] == 0:
        head += "   -   all done"
    if o["rate"] is not None:
        head += f"   |   {fmt_seconds(o['rate'])} per run"
    return head


def run_text(batches: list[Batch], interval: float, every_runs: int) -> None:
    last_done = -1
    try:
        while True:
            refresh(batches)
            now = time.time()
            rows, overall = build_rows(batches, now)
            if last_done < 0 or overall["done"] - last_done >= every_runs or not overall["running"]:
                last_done = overall["done"]
                print(f"\n[{_dt.datetime.now().strftime('%H:%M:%S')}] {overall_text(overall, now)}", flush=True)
                w = max(len(r.name) for r in rows)
                for r in rows:
                    print(f"  {r.name:<{w}}  {r.done:>4}/{r.total:<4} {r.percent:>7}  eta {r.eta:<8} ~{r.finish:<5}  {r.state}", flush=True)
            if overall["expected"] is not None and overall["remaining"] == 0 and not overall["running"]:
                print("all batches complete.", flush=True)
                return
            time.sleep(interval)
    except KeyboardInterrupt:
        return


def run_gui(batches: list[Batch], interval: float, every_runs: int, watched: list[str]) -> None:
    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    root.title("sim progress")
    root.geometry("1180x420")
    root.minsize(760, 240)

    top = ttk.Frame(root, padding=(10, 8))
    top.pack(fill="x")
    head_var = tk.StringVar(value="scanning…")
    ttk.Label(top, textvariable=head_var, font=("Segoe UI", 12, "bold")).pack(anchor="w")
    bar = ttk.Progressbar(top, orient="horizontal", mode="determinate", maximum=1000)
    bar.pack(fill="x", pady=(6, 0))

    cols = ("name", "total", "done", "running", "remaining", "percent", "per_run", "elapsed", "eta", "finish", "state")
    heads = {"name": "batch", "total": "total", "done": "done", "running": "running", "remaining": "left",
             "percent": "%", "per_run": "per run", "elapsed": "elapsed", "eta": "time left", "finish": "finish ~",
             "state": "state"}
    widths = {"name": 260, "total": 55, "done": 55, "running": 60, "remaining": 50, "percent": 65,
              "per_run": 70, "elapsed": 80, "eta": 85, "finish": 65, "state": 320}
    frame = ttk.Frame(root, padding=(10, 4))
    frame.pack(fill="both", expand=True)
    tree = ttk.Treeview(frame, columns=cols, show="headings", selectmode="none")
    for c in cols:
        tree.heading(c, text=heads[c])
        tree.column(c, width=widths[c], anchor="w" if c in ("name", "state") else "e", stretch=(c in ("name", "state")))
    vsb = ttk.Scrollbar(frame, orient="vertical", command=tree.yview)
    tree.configure(yscrollcommand=vsb.set)
    tree.pack(side="left", fill="both", expand=True)
    vsb.pack(side="right", fill="y")
    tree.tag_configure("complete", foreground="#2e7d32")
    tree.tag_configure("running", foreground="#1565c0")
    tree.tag_configure("failed", foreground="#c62828")
    tree.tag_configure("stalled", foreground="#e65100")

    bottom = ttk.Frame(root, padding=(10, 4))
    bottom.pack(fill="x")
    status_var = tk.StringVar(value="")
    ttk.Label(bottom, textvariable=status_var, foreground="#666").pack(anchor="w")

    state = {"last_redraw_done": -1, "last_scan": 0.0, "rows": [], "overall": None}

    def redraw(rows: list[Row], overall: dict, now: float) -> None:
        tree.delete(*tree.get_children())
        for r in rows:
            tag = ""
            if "FAILED" in r.state:
                tag = "failed"
            elif r.state.startswith("STALLED"):
                tag = "stalled"
            elif r.state == "complete":
                tag = "complete"
            elif r.state.startswith("running"):
                tag = "running"
            tree.insert("", "end", values=tuple(getattr(r, c) for c in cols), tags=(tag,))
        head_var.set(overall_text(overall, now))
        if overall["expected"]:
            bar["value"] = 1000.0 * overall["done"] / overall["expected"]
        else:
            bar["value"] = 0

    def tick() -> None:
        now = time.time()
        refresh(batches)
        rows, overall = build_rows(batches, now)
        finished = overall["expected"] is not None and overall["remaining"] == 0 and not overall["running"]
        due = (state["last_redraw_done"] < 0
               or overall["done"] - state["last_redraw_done"] >= every_runs
               or finished)
        if due:
            state["last_redraw_done"] = overall["done"]
            redraw(rows, overall, now)
        else:
            head_var.set(overall_text(overall, now))
        gate = f"redraw every {every_runs} runs, " if every_runs > 1 else ""
        status_var.set(f"{_dt.datetime.now().strftime('%H:%M:%S')}  scan every {interval:g}s, {gate}"
                       f"watching {', '.join(watched)}" + ("   -   ALL DONE" if finished else ""))
        root.after(int(interval * 1000), tick)

    tick()
    root.mainloop()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="campaign directories, or a sensitivity matrix directory with plan.json")
    ap.add_argument("--interval", type=float, default=5.0, help="seconds between scans (default 5)")
    ap.add_argument("--every-runs", type=int, default=1, help="redraw the table only after this many new completions")
    ap.add_argument("--text", action="store_true", help="terminal output instead of a window")
    ap.add_argument("--arms", default=None, help="expected arms for a batch without plan.json, e.g. static,radpr")
    ap.add_argument("--seeds", default=None, help="expected seeds for a batch without plan.json, e.g. 1..30")
    ap.add_argument("--rho", type=int, default=None, help="rho of the campaign (lets run ids be predicted)")
    args = ap.parse_args()

    if args.interval <= 0:
        sys.exit("--interval must be positive")
    if args.every_runs < 1:
        sys.exit("--every-runs must be at least 1")
    arms = [a.strip() for a in args.arms.split(",") if a.strip()] if args.arms else None
    seeds = parse_seeds(args.seeds) if args.seeds else None

    batches: list[Batch] = []
    for text in args.paths:
        p = Path(text)
        if not p.is_dir():
            sys.exit(f"not a directory: {text}")
        batches.extend(discover(p, arms, seeds, args.rho))
    if not batches:
        sys.exit("nothing to watch")

    if args.text:
        run_text(batches, args.interval, args.every_runs)
        return
    try:
        run_gui(batches, args.interval, args.every_runs, args.paths)
    except Exception as e:  # no display or no tkinter: fall back to text
        print(f"window unavailable ({e}); falling back to --text", file=sys.stderr, flush=True)
        run_text(batches, args.interval, args.every_runs)

if __name__ == "__main__":
    main()
