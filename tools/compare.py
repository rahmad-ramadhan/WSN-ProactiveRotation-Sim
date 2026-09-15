#!/usr/bin/env python3
"""Run arms over shared deployments and check reproducibility.

  campaign   run arms over deployments, write campaign.json and results.csv
  reproduce  re-run manifest entries and check each summary row matches
  tiers      compare two result sets: setup, static, other arms, then traces

Usage:
  python tools/compare.py campaign --arms static,radpr --deployments deployments --rho 400 --seeds 1..4 --out out/campaign
  python tools/compare.py reproduce --campaign out/campaign [--run RUN_ID ...] [--all]
  python tools/compare.py tiers --a out/campaign --b out/campaign_repro [--rename map.json]
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import hashlib
import math
import subprocess
import sys
from pathlib import Path

from gen_deployments import deployment_filename, parse_seeds
from simlib import ROOT, find_binary, launch_progress, read_json, read_summary, run_sim, write_json

MANIFEST = "campaign.json"
RESULTS = "results.csv"
NOT_COMPARED = ("wall_clock_s",)

TIER1_SCALARS = ["K", "mean_degree_gc", "max_degree_gi", "P_max", "E_0"]
TIER1_VECTORS = ["slot", "depth", "parent"]
TIER2_COLUMNS = ["fnd_frame", "adt_frame", "delivered_total", "energy_total", "partition_frame"]
TIER3_COLUMNS = ["rotations_total", "gap_p50", "partition_frame", "jain_index_fnd", "real_depth_max",
                 "parent_changes_total"]
TRACE_FIELDS = ["parent", "sent", "recv", "B", "E", "charge", "state"]


def rel(p: Path) -> str:
    """Repo-relative with forward slashes; absolute if outside the repo."""
    p = p.resolve()
    try:
        return p.relative_to(ROOT).as_posix()
    except ValueError:
        return p.as_posix()


def resolve(text: str) -> Path:
    p = Path(text)
    return p if p.is_absolute() else ROOT / p


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_commit() -> str:
    try:
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True, cwd=ROOT)
        return r.stdout.strip() if r.returncode == 0 else ""
    except OSError:
        return ""


def sim_version() -> str:
    p = run_sim(["--version"])
    return p.stdout.strip() if p.returncode == 0 else ""


def read_rows(path: Path) -> list[dict[str, str]]:
    with open(path, "r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def write_results(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(header)
        for r in rows:
            w.writerow([r[c] for c in header])


def values_equal(a: str, b: str, tol: float) -> bool:
    """Relative tolerance for numbers (nan == nan); exact match for text."""
    if a == b:
        return True
    try:
        x, y = float(a), float(b)
    except ValueError:
        return False
    if math.isnan(x) and math.isnan(y):
        return True
    if math.isinf(x) or math.isinf(y):
        return x == y
    return abs(x - y) <= tol * max(abs(x), abs(y))


def run_key(row: dict[str, str]) -> tuple[str, int, int]:
    return row["arm"], int(row["rho"]), int(row["seed"])


def key_text(k: tuple[str, int, int]) -> str:
    return f"{k[0]} rho={k[1]} seed={k[2]}"


def cmd_campaign(args: argparse.Namespace) -> int:
    out = resolve(args.out)
    dep_dir = resolve(args.deployments)
    arms = [a.strip() for a in args.arms.split(",") if a.strip()]
    seeds = parse_seeds(args.seeds)
    rho = int(args.rho)
    common = read_json(resolve(args.config)) if args.config else {}
    if "arm" in common:
        sys.exit("--config must not set \"arm\"; the campaign sets it per run")
    for k in ("seed", "positions"):
        if k in common:
            sys.exit(f"--config must not set \"{k}\"; the manifest carries it per run")
    out.mkdir(parents=True, exist_ok=True)
    if (out / MANIFEST).exists() and not args.force:
        sys.exit(f"{rel(out / MANIFEST)} exists; pass --force to overwrite the campaign")

    binary = find_binary()
    if not getattr(args, "no_progress", False):
        launch_progress([rel(out)], ["--arms", ",".join(arms), "--seeds", args.seeds, "--rho", str(rho)])
    manifest = {
        "label": args.label,
        "created": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"),
        "sim_version": sim_version(),
        "git_commit": git_commit(),
        "binary": rel(binary),
        "deployments_dir": rel(dep_dir),
        "config_common": common,
        "arms": arms,
        "rho": rho,
        "seeds": seeds,
        "runs": [],
    }
    header: list[str] | None = None
    rows: list[dict[str, str]] = []
    failures = 0
    for seed in seeds:
        pos = dep_dir / deployment_filename(rho, seed)
        if not pos.is_file():
            sys.exit(f"missing deployment {rel(pos)}; run tools/gen_deployments.py first")
        pos_sha = sha256_file(pos)
        for arm in arms:
            cfg = dict(common)
            cfg["arm"] = arm
            run_id = cfg.get("run_id") or f"{arm}_rho{rho}_s{seed}"
            cfg["rho"] = rho
            run_dir = out / run_id
            cfg_path = run_dir / "config_in.json"
            write_json(cfg_path, cfg)
            # Repo-relative, so the echoed config.json works on Windows and WSL.
            p = run_sim(["--config", str(cfg_path), "--seed", str(seed), "--positions", rel(pos),
                         "--out", str(run_dir)])
            entry = {
                "run_id": run_id, "arm": arm, "rho": rho, "seed": seed,
                "positions": rel(pos), "positions_sha256": pos_sha,
                "config": cfg, "out": rel(run_dir), "exit_code": p.returncode,
                "config_hash": "", "summary_sha256": "", "stderr": "",
            }
            if p.returncode == 0:
                s = read_summary(run_dir / "summary.csv")
                entry["config_hash"] = s["config_hash"]
                entry["summary_sha256"] = sha256_file(run_dir / "summary.csv")
                cols = list(s.keys())
                if header is None:
                    header = cols
                elif cols != header:
                    sys.exit(f"{run_id}: summary header differs from the first run's; columns must be one registry")
                rows.append(s)
                print(f"{run_id}: ok  fnd={s['fnd_frame']} partition={s['partition_frame']} "
                      f"pdr={s['pdr_global']} stop={s['stop_frame']} ({s['stop_reason']})")
            else:
                failures += 1
                entry["stderr"] = p.stderr.strip()[:1000]
                print(f"{run_id}: FAILED exit {p.returncode}: {p.stderr.strip()[:200]}")
            manifest["runs"].append(entry)
    manifest["runs_total"] = len(manifest["runs"])
    manifest["runs_failed"] = failures
    write_json(out / MANIFEST, manifest)
    if header is not None:
        write_results(out / RESULTS, header, rows)
    print(f"campaign: {len(manifest['runs'])} runs, {failures} failed -> {rel(out / MANIFEST)}"
          + (f", {rel(out / RESULTS)} ({len(rows)} rows)" if header else ""))
    return 1 if failures else 0


def reproduce_entry(entry: dict, out_dir: Path) -> tuple[bool, str]:
    """Re-run one manifest entry; True if the summary row is identical."""
    pos = ROOT / entry["positions"] if not Path(entry["positions"]).is_absolute() else Path(entry["positions"])
    if not pos.is_file():
        return False, f"positions file missing: {entry['positions']}"
    if sha256_file(pos) != entry["positions_sha256"]:
        return False, f"positions file changed since the campaign: {entry['positions']}"
    orig = ROOT / entry["out"] if not Path(entry["out"]).is_absolute() else Path(entry["out"])
    if not (orig / "summary.csv").is_file():
        return False, f"original summary missing: {entry['out']}/summary.csv"
    run_dir = out_dir / entry["run_id"]
    cfg_path = run_dir / "config_in.json"
    write_json(cfg_path, entry["config"])
    p = run_sim(["--config", str(cfg_path), "--seed", str(entry["seed"]), "--positions", rel(pos),
                 "--out", str(run_dir)])
    if p.returncode != 0:
        return False, f"re-run failed exit {p.returncode}: {p.stderr.strip()[:300]}"
    a = read_summary(orig / "summary.csv")
    b = read_summary(run_dir / "summary.csv")
    if list(a.keys()) != list(b.keys()):
        return False, "summary header differs between the campaign and the re-run"
    diff = [c for c in a if c not in NOT_COMPARED and a[c] != b[c]]
    if diff:
        return False, "differs in " + ", ".join(f"{c} ({a[c]} vs {b[c]})" for c in diff[:8])
    return True, "identical"


def cmd_reproduce(args: argparse.Namespace) -> int:
    camp = resolve(args.campaign)
    manifest = read_json(camp / MANIFEST)
    out = resolve(args.out) if args.out else camp.parent / (camp.name + "_repro")
    entries = manifest["runs"]
    if args.run:
        wanted = set(args.run)
        entries = [e for e in entries if e["run_id"] in wanted]
        missing = wanted - {e["run_id"] for e in entries}
        if missing:
            sys.exit(f"not in the manifest: {', '.join(sorted(missing))}")
    elif not args.all:
        entries = entries[:1]
    entries = [e for e in entries if e["exit_code"] == 0]
    if not entries:
        sys.exit("nothing to reproduce (no completed runs selected)")
    header: list[str] | None = None
    rows: list[dict[str, str]] = []
    bad = 0
    for e in entries:
        ok, why = reproduce_entry(e, out)
        print(f"{e['run_id']}: {'REPRODUCED' if ok else 'MISMATCH'} -- {why}")
        if not ok:
            bad += 1
            continue
        s = read_summary(out / e["run_id"] / "summary.csv")
        header = header or list(s.keys())
        rows.append(s)
    if header:
        write_results(out / RESULTS, header, rows)
    repro_manifest = {"reproduces": rel(camp / MANIFEST), "runs": [e["run_id"] for e in entries],
                      "mismatches": bad, "sim_version": sim_version(), "git_commit": git_commit()}
    write_json(out / "reproduce.json", repro_manifest)
    print(f"reproduce: {len(entries) - bad}/{len(entries)} identical -> {rel(out)}")
    return 1 if bad else 0


class Side:

    def __init__(self, root: Path, rename: dict[str, str]):
        self.root = root
        self.rows: dict[tuple[str, int, int], dict[str, str]] = {}
        self.dirs: dict[tuple[str, int, int], Path | None] = {}
        rows: list[dict[str, str]] = []
        if (root / RESULTS).is_file():
            rows = read_rows(root / RESULTS)
        else:
            for s in sorted(root.glob("*/summary.csv")):
                rows.append(read_summary(s))
        if not rows:
            sys.exit(f"{rel(root)}: no results.csv and no */summary.csv")
        for r in rows:
            r = {rename.get(k, k): v for k, v in r.items()}
            for need in ("arm", "rho", "seed"):
                if need not in r:
                    sys.exit(f"{rel(root)}: rows lack column {need!r}; use --rename")
            k = run_key(r)
            if k in self.rows:
                sys.exit(f"{rel(root)}: two rows for {key_text(k)}")
            self.rows[k] = r
            d = None
            if r.get("run_id") and (root / r["run_id"]).is_dir():
                d = root / r["run_id"]
            else:
                cand = root / f"{k[0]}_rho{k[1]}_s{k[2]}"
                d = cand if cand.is_dir() else None
            self.dirs[k] = d

    def topology(self, k: tuple[str, int, int]) -> dict[int, dict[str, str]] | None:
        d = self.dirs.get(k)
        if d is None or not (d / "topology.csv").is_file():
            return None
        return {int(r["id"]): r for r in read_rows(d / "topology.csv")}

    def trace_path(self, k: tuple[str, int, int]) -> Path | None:
        d = self.dirs.get(k)
        if d is None or not (d / "trace.csv").is_file():
            return None
        return d / "trace.csv"


class Report:
    def __init__(self, max_lines: int):
        self.max_lines = max_lines
        self.tier_mismatches: dict[str, int] = {}
        self.tier_checked: dict[str, int] = {}
        self._lines: dict[str, list[str]] = {}

    def start(self, tier: str) -> None:
        self.tier_mismatches[tier] = 0
        self.tier_checked[tier] = 0
        self._lines[tier] = []

    def record(self, tier: str, ok: bool, text: str) -> None:
        self.tier_checked[tier] += 1
        if not ok:
            self.tier_mismatches[tier] += 1
            if len(self._lines[tier]) < self.max_lines:
                self._lines[tier].append(text)

    def finish(self, tier: str, title: str) -> bool:
        n, m = self.tier_checked[tier], self.tier_mismatches[tier]
        print(f"--- {title}: {n - m}/{n} matched, {m} mismatches ---")
        for ln in self._lines[tier]:
            print("    " + ln)
        if m > len(self._lines[tier]):
            print(f"    ... {m - len(self._lines[tier])} more")
        return m == 0


def compare_columns(rep: Report, tier: str, a: Side, b: Side, keys: list, columns: list[str], tol: float) -> None:
    for k in keys:
        ra, rb = a.rows[k], b.rows[k]
        for c in columns:
            if c not in ra or c not in rb:
                rep.record(tier, False, f"{key_text(k)}: column {c!r} missing on "
                           + ("A" if c not in ra else "B") + " (use --rename?)")
                continue
            ok = values_equal(ra[c], rb[c], tol)
            rep.record(tier, ok, f"{key_text(k)}: {c} A={ra[c]} B={rb[c]}")


def tier1(rep: Report, a: Side, b: Side, keys: list, tol: float) -> bool:
    rep.start("t1")
    compare_columns(rep, "t1", a, b, keys, TIER1_SCALARS, tol)
    vec_runs = 0
    for k in keys:
        ta, tb = a.topology(k), b.topology(k)
        if ta is None or tb is None:
            continue
        vec_runs += 1
        ids = sorted(set(ta) | set(tb))
        for col in TIER1_VECTORS:
            first_bad = None
            bad = 0
            for i in ids:
                va = ta.get(i, {}).get(col)
                vb = tb.get(i, {}).get(col)
                if va != vb:
                    bad += 1
                    if first_bad is None:
                        first_bad = (i, va, vb)
            rep.record("t1", bad == 0,
                       f"{key_text(k)}: {col} vector differs at {bad} ids; first id {first_bad[0]} "
                       f"A={first_bad[1]} B={first_bad[2]}" if bad else "")
    ok = rep.finish("t1", f"Tier 1 - setup: {', '.join(TIER1_SCALARS)}; vectors {', '.join(TIER1_VECTORS)} "
                          f"on {vec_runs} runs with topology.csv on both sides")
    if vec_runs == 0:
        print("    (no run had topology.csv on both sides; the vectors were not compared)")
    return ok


def tier2(rep: Report, a: Side, b: Side, keys: list, tol: float) -> bool:
    rep.start("t2")
    ks = [k for k in keys if k[0] == "static"]
    compare_columns(rep, "t2", a, b, ks, TIER2_COLUMNS, tol)
    ok = rep.finish("t2", f"Tier 2 - static ({len(ks)} runs): {', '.join(TIER2_COLUMNS)}")
    if not ks:
        print("    (no static run paired; Tier 2 was not exercised)")
    return ok


def tier3(rep: Report, a: Side, b: Side, keys: list, tol: float) -> tuple[bool, list]:
    rep.start("t3")
    ks = [k for k in keys if k[0] != "static"]
    compare_columns(rep, "t3", a, b, ks, TIER3_COLUMNS, tol)
    ok = rep.finish("t3", f"Tier 3 - {', '.join(sorted({k[0] for k in ks}))} ({len(ks)} runs): "
                          f"{', '.join(TIER3_COLUMNS)}")
    mism = [k for k in ks if any(c in a.rows[k] and c in b.rows[k]
                                 and not values_equal(a.rows[k][c], b.rows[k][c], tol) for c in TIER3_COLUMNS)]
    return ok, mism


def first_trace_difference(pa: Path, pb: Path, tol: float) -> str:
    """First (frame, node) where the traces differ."""
    with open(pa, "r", encoding="utf-8", newline="") as fa, open(pb, "r", encoding="utf-8", newline="") as fb:
        ra, rb = csv.DictReader(fa), csv.DictReader(fb)
        common = [c for c in TRACE_FIELDS if c in (ra.fieldnames or []) and c in (rb.fieldnames or [])]
        if not common:
            return "traces share no comparable column"
        n = 0
        for xa, xb in zip(ra, rb):
            n += 1
            if (xa.get("f"), xa.get("id")) != (xb.get("f"), xb.get("id")):
                return f"row {n}: A is (f={xa.get('f')}, id={xa.get('id')}) but B is (f={xb.get('f')}, id={xb.get('id')}); traces are not aligned"
            for c in common:
                if not values_equal(xa[c], xb[c], tol):
                    return f"first difference at f={xa['f']} id={xa['id']} in {c}: A={xa[c]} B={xb[c]}"
        na = n + sum(1 for _ in ra)
        nb = n + sum(1 for _ in rb)
        if na != nb:
            return f"identical over {n} shared rows; A has {na} rows, B has {nb}"
        return f"identical over all {n} rows on {', '.join(common)}"


def tier4(a: Side, b: Side, keys: list, tol: float) -> None:
    print("--- Tier 4 - trace diff on mismatching runs ---")
    if not keys:
        print("    nothing to diff")
        return
    for k in keys:
        pa, pb = a.trace_path(k), b.trace_path(k)
        if pa is None or pb is None:
            print(f"    {key_text(k)}: no trace.csv on "
                  + ("both sides" if pa is None and pb is None else "A" if pa is None else "B")
                  + " (re-run with trace_frames > 0 to localise)")
            continue
        print(f"    {key_text(k)}: {first_trace_difference(pa, pb, tol)}")


def cmd_tiers(args: argparse.Namespace) -> int:
    rename = read_json(resolve(args.rename)) if args.rename else {}
    a = Side(resolve(args.a), {})
    b = Side(resolve(args.b), rename)
    keys = sorted(set(a.rows) & set(b.rows))
    only_a = sorted(set(a.rows) - set(b.rows))
    only_b = sorted(set(b.rows) - set(a.rows))
    print(f"A: {rel(a.root)} ({len(a.rows)} runs)   B: {rel(b.root)} ({len(b.rows)} runs)   paired: {len(keys)}")
    if only_a:
        print("    only in A: " + ", ".join(key_text(k) for k in only_a[:10]) + (" ..." if len(only_a) > 10 else ""))
    if only_b:
        print("    only in B: " + ", ".join(key_text(k) for k in only_b[:10]) + (" ..." if len(only_b) > 10 else ""))
    if not keys:
        sys.exit("no run is present on both sides (match is by arm, rho, seed)")
    if "sim_version" in next(iter(a.rows.values())) and "sim_version" in next(iter(b.rows.values())):
        va = sorted({r.get("sim_version", "") for r in a.rows.values()})
        vb = sorted({r.get("sim_version", "") for r in b.rows.values()})
        print(f"    sim_version A={va} B={vb}")

    rep = Report(args.max_lines)
    failed = False
    ok1 = tier1(rep, a, b, keys, args.tol)
    failed |= not ok1
    if ok1 or args.all_tiers:
        ok2 = tier2(rep, a, b, keys, args.tol)
        failed |= not ok2
        if ok2 or args.all_tiers:
            ok3, mism = tier3(rep, a, b, keys, args.tol)
            failed |= not ok3
            if not ok3 or args.all_tiers:
                tier4(a, b, mism, args.tol)
        else:
            print("Tier 2 failed: an accounting difference; Tier 3 not run (pass --all-tiers to force)")
    else:
        print("Tier 1 failed: a setup difference; everything downstream is meaningless until it is fixed "
              "(pass --all-tiers to run the rest anyway)")
    print("tiers: " + ("MISMATCH" if failed else "all compared tiers match"))
    return 1 if failed else 0


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("campaign", help="run arms x seeds on shared deployments; write manifest and results.csv")
    c.add_argument("--arms", required=True, help="comma list, e.g. static,radpr,nemcr,dcfr,escfr")
    c.add_argument("--deployments", default="deployments", help="directory written by gen_deployments.py")
    c.add_argument("--rho", default="400")
    c.add_argument("--seeds", default="1..30", help="range a..b or comma list")
    c.add_argument("--out", required=True, help="campaign directory (relative to the repo root)")
    c.add_argument("--config", default="", help="JSON of extra config keys shared by every run (no arm/seed/positions)")
    c.add_argument("--label", default="", help="free text stored in the manifest")
    c.add_argument("--force", action="store_true", help="overwrite an existing campaign directory's manifest")
    c.add_argument("--no-progress", action="store_true", help="do not open the progress window (tools/progress.py)")
    c.set_defaults(fn=cmd_campaign)

    r = sub.add_parser("reproduce", help="re-run manifest entries and confirm identical summary rows")
    r.add_argument("--campaign", required=True, help="campaign directory holding campaign.json")
    r.add_argument("--run", action="append", help="run_id to reproduce (repeatable); default: the first entry")
    r.add_argument("--all", action="store_true", help="reproduce every completed entry")
    r.add_argument("--out", default="", help="where the re-runs go (default <campaign>_repro)")
    r.set_defaults(fn=cmd_reproduce)

    t = sub.add_parser("tiers", help="compare two result sets tier by tier")
    t.add_argument("--a", required=True, help="result set A (this build's campaign directory)")
    t.add_argument("--b", required=True, help="result set B (e.g. a reproduction)")
    t.add_argument("--rename", default="", help="JSON {their_column: our_column} applied to side B")
    t.add_argument("--tol", type=float, default=1e-12, help="relative tolerance for numeric columns (0 = exact)")
    t.add_argument("--all-tiers", action="store_true", help="run every tier even after one fails")
    t.add_argument("--max-lines", type=int, default=40, help="mismatch lines printed per tier")
    t.set_defaults(fn=cmd_tiers)

    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
