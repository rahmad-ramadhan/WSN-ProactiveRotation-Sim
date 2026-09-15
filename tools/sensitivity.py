#!/usr/bin/env python3
"""One-factor-at-a-time sensitivity matrix over the variant config keys.

  run      run the reference and each cell as a compare.py campaign
  report   pair each cell with the reference by seed and write sensitivity.csv,
           rankings.csv, counters.csv, falsification.csv, sensitivity.md/.json

`not reached` frames count as later than any reached frame.

Usage:
  python tools/sensitivity.py run --deployments deployments --seeds 1..30 --out out/sensitivity [--cells S0,S10]
  python tools/sensitivity.py report --out out/sensitivity
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import math
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path

from compare import MANIFEST, RESULTS, cmd_campaign, read_rows, rel, resolve
from simlib import launch_progress
from gen_deployments import parse_seeds
from simlib import read_json, write_json

ALL_ARMS = ["static", "radpr", "nemcr", "dcfr", "escfr"]
PROPOSED = "radpr"
NOT_REACHED = "not reached"
FIRST_ORPHAN = "first_orphan_frame"        # derived from frames.csv


@dataclass(frozen=True)
class Cell:
    id: str
    key: str | None           # None for the reference
    value: object
    arms: tuple[str, ...]
    counters: tuple[str, ...]
    named: bool = False
    mandatory: bool = False
    part_f: bool = False

    @property
    def label(self) -> str:
        return "reference" if self.key is None else f"{self.key} = {self.value}"

    @property
    def dirname(self) -> str:
        return f"{self.id}_reference" if self.key is None else f"{self.id}_{self.key}_{self.value}"


CELLS: list[Cell] = [
    Cell("S0", None, None, tuple(ALL_ARMS), ()),
    Cell("S1", "dcfr_cost_attribution", "neighbour", ("dcfr", "escfr"), ("dcfr_cost_max", "parent_changes_total")),
    Cell("S2", "escfr_energy_map", "eq9_10", ("dcfr", "escfr"), ("dcfr_cost_max", "parent_changes_total")),
    Cell("S3", "dcfr_rate_normaliser", "network", ("dcfr",), ("dcfr_rule1_bindings",)),
    Cell("S4", "dcfr_rate_normaliser", "calibration", ("dcfr",), ("dcfr_rule1_bindings",)),
    Cell("S5", "dcfr_rate_window", "M", ("dcfr",), ("dcfr_rule1_bindings",), named=True),
    Cell("S6", "dcfr_period", "M", ("dcfr", "escfr"), ("control_msgs", "parent_changes_total")),
    Cell("S7", "nemcr_backup_refresh", "on_failure", ("nemcr",), ("nemcr_stage_a", "nemcr_stale_plans")),
    Cell("S8", "nemcr_slot_inherit", "off", ("nemcr",), ("slot_inheritances", "colouring_violations"), named=True),
    Cell("S9", "radpr_proximity", "linear", ("radpr",), ("rotations_total",)),
    Cell("S10", "Gamma", 0, ("radpr",), ("rotations_total",), named=True, mandatory=True),
    Cell("S11", "control_cost_model", "costed", ("radpr", "dcfr", "escfr"), ("control_bits_frac", "control_energy"),
         named=True, mandatory=True),
    Cell("S12", "knowledge_mode", "oracle", ("radpr", "dcfr", "escfr"), ("parent_changes_total",)),
    Cell("S13", "topology_profile", "paper", tuple(ALL_ARMS), ("nemcr_out_of_reach_frac", "nemcr_stage_a"), part_f=True),
    Cell("S14", "radpr_backups", "on", ("radpr",), ("recoveries_backup", "backups_assigned")),
    Cell("S15", "nemcr_backup_refresh", "frozen", ("nemcr",), ("nemcr_stale_plans", "nemcr_stage_b")),
]
CELL_BY_ID = {c.id: c for c in CELLS}
REFERENCE = CELL_BY_ID["S0"]

DEFAULT_METRICS = ["partition_frame", FIRST_ORPHAN, "fnd_frame", "pdr_to_fnd", "jain_index_fnd"]
METRIC_NOTE = {
    "partition_frame": "stop condition (routed == 0); censored at T_stop",
    FIRST_ORPHAN: "first frame with an orphaned alive node, derived from frames.csv (earliest partition reading)",
    "fnd_frame": "first node death",
    "pdr_to_fnd": "delivery ratio up to first death",
    "jain_index_fnd": "Jain fairness of consumption at first death",
}


def to_value(text: str) -> float:
    """'not reached' -> +inf; unparsable -> nan."""
    if text == NOT_REACHED:
        return math.inf
    try:
        return float(text)
    except ValueError:
        return math.nan


def fmt(x: float, metric: str) -> str:
    if math.isnan(x):
        return "nan"
    if math.isinf(x):
        return "not reached"
    if metric in ("partition_frame", FIRST_ORPHAN, "fnd_frame") or abs(x) >= 100:
        return f"{x:.0f}"
    return f"{x:.4f}"

def fmt_signed(x: float, metric: str) -> str:
    if math.isnan(x):
        return "n/a"
    s = fmt(abs(x), metric)
    return ("+" if x > 0 else "-" if x < 0 else "") + s


def first_orphan_frame(run_dir: Path) -> float:
    """First frame with an orphan in frames.csv; +inf if none, nan if no file."""
    p = run_dir / "frames.csv"
    if not p.is_file():
        return math.nan
    with open(p, "r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if int(row["orphan"]) > 0:
                return float(row["f"])
    return math.inf


def cell_complete(cell_dir: Path, arms: list[str], seeds: list[int]) -> bool:
    man_path = cell_dir / MANIFEST
    if not man_path.is_file() or not (cell_dir / RESULTS).is_file():
        return False
    man = read_json(man_path)
    return (man.get("runs_failed", 1) == 0 and man.get("arms") == arms and man.get("seeds") == seeds
            and man.get("runs_total") == len(arms) * len(seeds))


def cmd_run(args: argparse.Namespace) -> int:
    out = resolve(args.out)
    out.mkdir(parents=True, exist_ok=True)
    seeds = parse_seeds(args.seeds)
    common = read_json(resolve(args.config)) if args.config else {}
    wanted = [c.strip() for c in args.cells.split(",") if c.strip()] if args.cells else [c.id for c in CELLS]
    for w in wanted:
        if w not in CELL_BY_ID:
            sys.exit(f"unknown cell {w!r}; valid: {', '.join(c.id for c in CELLS)}")
    for cell in CELLS:
        if cell.key is not None and cell.key in common:
            sys.exit(f"--config sets {cell.key!r}, which cell {cell.id} varies; the reference must keep every sensitivity key at its default")
    plan = {
        "created": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"),
        "deployments": rel(resolve(args.deployments)), "rho": int(args.rho), "seeds": seeds,
        "config_common": common, "all_arms": bool(args.all_arms),
        "cells": [{"id": c.id, "key": c.key, "value": c.value, "arms": list(ALL_ARMS if args.all_arms else c.arms),
                   "dir": c.dirname, "part_f": c.part_f, "mandatory": c.mandatory} for c in CELLS],
    }
    write_json(out / "plan.json", plan)
    if not args.no_progress:
        launch_progress([rel(out)])
    failed = 0
    for cell in CELLS:
        if cell.id not in wanted:
            continue
        arms = list(ALL_ARMS if args.all_arms else cell.arms)
        cell_dir = out / cell.dirname
        if not args.force and cell_complete(cell_dir, arms, seeds):
            print(f"[{cell.id}] {cell.label}: complete, skipped ({len(arms) * len(seeds)} runs)")
            continue
        cfg = dict(common)
        if cell.key is not None:
            cfg[cell.key] = cell.value
        cfg_path = cell_dir / "cell_config.json"
        write_json(cfg_path, cfg)
        print(f"[{cell.id}] {cell.label}: arms {','.join(arms)} x {len(seeds)} seeds -> {rel(cell_dir)}")
        rc = cmd_campaign(argparse.Namespace(
            out=str(cell_dir), deployments=args.deployments, arms=",".join(arms), seeds=args.seeds, rho=args.rho,
            config=str(cfg_path), label=f"sensitivity {cell.id}: {cell.label}", force=True, no_progress=True))
        if rc != 0:
            failed += 1
            print(f"[{cell.id}] FAILED runs; see {rel(cell_dir / MANIFEST)}")
    print(f"run: {len(wanted)} cells, {failed} with failed runs")
    return 1 if failed else 0


class CellData:

    def __init__(self, cell: Cell, cell_dir: Path):
        self.cell = cell
        self.dir = cell_dir
        self.rows: dict[tuple[str, int], dict[str, str]] = {}
        if not (cell_dir / RESULTS).is_file():
            return
        for r in read_rows(cell_dir / RESULTS):
            run_dir = cell_dir / r["run_id"]
            r[FIRST_ORPHAN] = fmt(first_orphan_frame(run_dir), FIRST_ORPHAN) if run_dir.is_dir() else "nan"
            self.rows[(r["arm"], int(r["seed"]))] = r

    def arms(self) -> list[str]:
        return sorted({a for a, _ in self.rows}, key=ALL_ARMS.index)

    def seeds(self) -> list[int]:
        return sorted({s for _, s in self.rows})

    def value(self, arm: str, seed: int, col: str) -> float:
        r = self.rows.get((arm, seed))
        return math.nan if r is None or col not in r else to_value(r[col])


def paired_delta(ref: CellData, cell: CellData, arm: str, metric: str, seeds: list[int]) -> dict:
    """cell - reference per seed, classified as numeric, censored or undecidable."""
    deltas: list[float] = []
    later = earlier = both_censored = fnd_censored = missing = 0
    for s in seeds:
        a, b = ref.value(arm, s, metric), cell.value(arm, s, metric)
        if math.isnan(a) or math.isnan(b):
            # pdr_to_fnd and jain_index_fnd are undefined when no node died before T_stop
            if math.isinf(ref.value(arm, s, "fnd_frame")) or math.isinf(cell.value(arm, s, "fnd_frame")):
                fnd_censored += 1
            else:
                missing += 1
        elif math.isinf(a) and math.isinf(b):
            both_censored += 1
        elif math.isinf(b):          # cell not reached: later
            later += 1
        elif math.isinf(a):
            earlier += 1
        else:
            deltas.append(b - a)
    pos = sum(1 for d in deltas if d > 0)
    neg = sum(1 for d in deltas if d < 0)
    zero = len(deltas) - pos - neg
    mean = statistics.mean(deltas) if deltas else math.nan
    if deltas or later or earlier:
        up, down = pos + later, neg + earlier
        sign = "+" if up > down else "-" if down > up else "0" if (up == down == 0) else "±"
    else:
        sign = "n/a"
    ref_vals = [ref.value(arm, s, metric) for s in seeds]
    cell_vals = [cell.value(arm, s, metric) for s in seeds]
    fin = lambda xs: [x for x in xs if not math.isnan(x) and not math.isinf(x)]  # noqa: E731
    return {
        "n_seeds": len(seeds), "n_numeric": len(deltas), "n_pos": pos, "n_neg": neg, "n_zero": zero,
        "n_cell_later_censored": later, "n_cell_earlier_censored": earlier,
        "n_both_censored": both_censored, "n_fnd_censored": fnd_censored, "n_missing": missing,
        "mean_delta": mean, "median_delta": statistics.median(deltas) if deltas else math.nan,
        "sign": sign, "all_zero": bool(deltas) and pos == neg == later == earlier == 0,
        "ref_mean": statistics.mean(fin(ref_vals)) if fin(ref_vals) else math.nan,
        "cell_mean": statistics.mean(fin(cell_vals)) if fin(cell_vals) else math.nan,
        "ref_reached": sum(1 for x in ref_vals if not math.isnan(x) and not math.isinf(x)),
        "cell_reached": sum(1 for x in cell_vals if not math.isnan(x) and not math.isinf(x)),
    }


def mean_ranks(values: dict[str, list[float]], seeds_n: int) -> dict[str, float]:
    """Mean rank per arm, rank 1 = highest, ties averaged. Seeds with any nan are skipped."""
    arms = list(values)
    total = {a: 0.0 for a in arms}
    used = 0
    for i in range(seeds_n):
        col = {a: values[a][i] for a in arms}
        if any(math.isnan(v) for v in col.values()):
            continue
        used += 1
        order = sorted(arms, key=lambda a: -col[a])
        j = 0
        while j < len(order):
            k = j
            while k + 1 < len(order) and col[order[k + 1]] == col[order[j]]:
                k += 1
            r = (j + 1 + k + 1) / 2.0
            for a in order[j:k + 1]:
                total[a] += r
            j = k + 1
    return {a: (total[a] / used if used else math.nan) for a in arms}


def order_text(ranks: dict[str, float]) -> str:
    arms = sorted(ranks, key=lambda a: (ranks[a], ALL_ARMS.index(a)))
    out = arms[0]
    for prev, cur in zip(arms, arms[1:]):
        out += (" = " if abs(ranks[prev] - ranks[cur]) < 1e-9 else " > ") + cur
    return out


def ranking(ref: CellData, cell: CellData | None, metric: str, seeds: list[int], arms: list[str]) -> dict:
    ref_vals = {a: [ref.value(a, s, metric) for s in seeds] for a in ALL_ARMS}
    r_ref = mean_ranks(ref_vals, len(seeds))
    if cell is None:
        return {"ref_order": order_text(r_ref), "ref_ranks": r_ref}
    cell_vals = dict(ref_vals)
    for a in arms:
        cell_vals[a] = [cell.value(a, s, metric) for s in seeds]
    r_cell = mean_ranks(cell_vals, len(seeds))
    pos = lambda r, a: 1 + sum(1 for b in r if r[b] < r[a] - 1e-9)  # noqa: E731
    p_ref, p_cell = pos(r_ref, PROPOSED), pos(r_cell, PROPOSED)
    return {
        "ref_order": order_text(r_ref), "cell_order": order_text(r_cell),
        "changed": order_text(r_ref) != order_text(r_cell),
        "radpr_position_ref": p_ref, "radpr_position_cell": p_cell,
        "radpr_standing": "better" if p_cell < p_ref else "worse" if p_cell > p_ref else "same",
        "ref_ranks": r_ref, "cell_ranks": r_cell,
    }


def counter_row(ref: CellData, cell: CellData, arm: str, counter: str, seeds: list[int]) -> dict:
    a = [ref.value(arm, s, counter) for s in seeds]
    b = [cell.value(arm, s, counter) for s in seeds]
    fin = lambda xs: [x for x in xs if not math.isnan(x) and not math.isinf(x)]  # noqa: E731
    return {"ref_mean": statistics.mean(fin(a)) if fin(a) else math.nan,
            "cell_mean": statistics.mean(fin(b)) if fin(b) else math.nan,
            "n_changed": sum(1 for x, y in zip(a, b) if not (x == y or (math.isnan(x) and math.isnan(y))))}


def majority(wins: int, losses: int) -> str:
    if wins + losses == 0:
        return "NOT TESTABLE"
    return "HOLDS" if wins > losses else "FAILS"


def compare_arms(data: CellData, seeds: list[int], a: str, b: str, col: str, want: str) -> tuple[int, int, int]:
    """(wins, losses, undecided) of arm a over arm b on col."""
    w = l = u = 0
    for s in seeds:
        x, y = data.value(a, s, col), data.value(b, s, col)
        if math.isnan(x) or math.isnan(y) or x == y:
            u += 1
        elif (x > y) == (want == "higher"):
            w += 1
        else:
            l += 1
    return w, l, u


def falsification(ref: CellData, paper: CellData | None, seeds: list[int]) -> list[dict]:
    rows: list[dict] = []

    def add(fid: str, claim: str, profile: str, data: CellData | None, test: str, w: int, l: int, u: int,
            verdict: str | None = None) -> None:
        rows.append({"id": fid, "claim": claim, "profile": profile, "test": test,
                     "holds_on": w, "fails_on": l, "undecided": u,
                     "verdict": verdict or ("NOT TESTABLE" if data is None else majority(w, l))})

    for prof, data in (("shared", ref), ("paper", paper)):
        if data is None:
            for fid, claim in (("F-1", "DCFR not inferior to ESCFR in energy balancing (Liu Lemma 5)"),
                               ("F-3", "DCFR and ESCFR outlive a distance-only baseline (Liu §5.1)"),
                               ("F-4", "NE-MCR partitions later than DCFR and ESCFR (Urmonov §5.2)"),
                               ("F-5", "NE-MCR spends less distance energy per node than DCFR and ESCFR (Urmonov §5.2)"),
                               ("F-6", "NE-MCR keeps more nodes connected: fewer orphan-frames per node (Urmonov §5.1)"),
                               ("F-8", "recovery needs no rescheduling: slot_inheritances > 0, I8 held (Urmonov §3.1)")):
                add(fid, claim, prof, None, "no runs for this profile", 0, 0, 0)
            continue
        w, l, u = compare_arms(data, seeds, "dcfr", "escfr", "jain_index_fnd", "higher")
        add("F-1", "DCFR not inferior to ESCFR in energy balancing (Liu Lemma 5)", prof, data,
            "jain_index_fnd(dcfr) >= jain_index_fnd(escfr) on a majority of seeds", w + u, l, 0,
            majority(w + u, l))
        for col, label in (("partition_frame", "partition_frame"), (FIRST_ORPHAN, "first_orphan_frame")):
            w1, l1, u1 = compare_arms(data, seeds, "dcfr", "static", col, "higher")
            w2, l2, u2 = compare_arms(data, seeds, "escfr", "static", col, "higher")
            add("F-3", "DCFR and ESCFR outlive a distance-only baseline (Liu §5.1)", prof, data,
                f"{label}: dcfr > static {w1}/{l1}/{u1}, escfr > static {w2}/{l2}/{u2} (wins/losses/undecided)",
                w1 + w2, l1 + l2, u1 + u2)
            w1, l1, u1 = compare_arms(data, seeds, "nemcr", "dcfr", col, "higher")
            w2, l2, u2 = compare_arms(data, seeds, "nemcr", "escfr", col, "higher")
            add("F-4", "NE-MCR partitions later than DCFR and ESCFR (Urmonov §5.2, Fig. 12a)", prof, data,
                f"{label}: nemcr > dcfr {w1}/{l1}/{u1}, nemcr > escfr {w2}/{l2}/{u2}", w1 + w2, l1 + l2, u1 + u2)
        w1, l1, u1 = compare_arms(data, seeds, "nemcr", "dcfr", "energy_distance_component", "lower")
        w2, l2, u2 = compare_arms(data, seeds, "nemcr", "escfr", "energy_distance_component", "lower")
        add("F-5", "NE-MCR spends less distance energy per node than DCFR and ESCFR (Urmonov §5.2, Fig. 12b)", prof, data,
            f"energy_distance_component lower: vs dcfr {w1}/{l1}/{u1}, vs escfr {w2}/{l2}/{u2}", w1 + w2, l1 + l2, u1 + u2)
        w1, l1, u1 = compare_arms(data, seeds, "nemcr", "dcfr", "orphan_frames_total", "lower")
        w2, l2, u2 = compare_arms(data, seeds, "nemcr", "escfr", "orphan_frames_total", "lower")
        add("F-6", "NE-MCR keeps more nodes connected: fewer orphan-frames per node (Urmonov §5.1, Fig. 11a)", prof, data,
            f"orphan_frames_total lower: vs dcfr {w1}/{l1}/{u1}, vs escfr {w2}/{l2}/{u2}", w1 + w2, l1 + l2, u1 + u2)
        inh = [data.value("nemcr", s, "slot_inheritances") for s in seeds]
        ok = sum(1 for x in inh if x > 0)
        add("F-8", "recovery needs no rescheduling: slot_inheritances > 0 and no slot write outside them (I8 fatal, run completed)",
            prof, data, f"nemcr slot_inheritances > 0 on {ok}/{len(seeds)} seeds; every run completed so I8 held",
            ok, len(seeds) - ok, 0)
    rows.append({"id": "F-2", "claim": "DCFR reaches energy balance earlier than ESCFR (Liu §4.4)", "profile": "both",
                 "test": "needs the per-frame Jain index; not a summary column", "holds_on": 0, "fails_on": 0,
                 "undecided": 0, "verdict": "NOT TESTABLE"})
    rows.append({"id": "F-7", "claim": "NE-MCR's isolated-node count grows linearly, not exponentially (Urmonov §5.1)",
                 "profile": "both", "test": "needs a fit of frames.csv orphan against frame after FND; not done here",
                 "holds_on": 0, "fails_on": 0, "undecided": 0, "verdict": "NOT TESTABLE"})
    rows.sort(key=lambda r: (int(r["id"].split("-")[1]), r["profile"]))
    return rows


def write_csv(path: Path, header: list[str], rows: list[dict]) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(header)
        for r in rows:
            w.writerow([("" if isinstance(r.get(c), float) and math.isnan(r[c]) else r.get(c, "")) for c in header])


def delta_cell_text(d: dict, metric: str) -> str:
    if d["n_numeric"] == 0 and d["n_cell_later_censored"] == 0 and d["n_cell_earlier_censored"] == 0:
        return f"n/a (censored on {d['n_both_censored'] + d['n_fnd_censored']}/{d['n_seeds']})"
    parts = []
    if d["n_numeric"]:
        parts.append(f"{fmt_signed(d['mean_delta'], metric)} on {d['n_numeric']} ({d['n_pos']}+ {d['n_neg']}- {d['n_zero']}=)")
    if d["n_cell_later_censored"]:
        parts.append(f"later-censored {d['n_cell_later_censored']}")
    if d["n_cell_earlier_censored"]:
        parts.append(f"earlier-censored {d['n_cell_earlier_censored']}")
    if d["n_both_censored"]:
        parts.append(f"both censored {d['n_both_censored']}")
    if d["n_fnd_censored"]:
        parts.append(f"undefined, no death before T_stop {d['n_fnd_censored']}")
    return "; ".join(parts)


def write_markdown(path: Path, rep: dict, metrics: list[str]) -> None:
    L: list[str] = []
    L.append("# Sensitivity table - one factor at a time\n")
    L.append(f"Generated {rep['created']} from `{rep['out']}`; rho={rep['rho']}, seeds {rep['seeds'][0]}..{rep['seeds'][-1]} "
             f"({len(rep['seeds'])} shared deployments); sim {rep['sim_version']}, commit {rep['git_commit']}.\n")
    L.append("Every delta is **cell minus reference**, paired by seed on the same deployment. Metrics are "
             "higher-is-better. `later-censored n` counts seeds where the reference reached the frame and the cell "
             "did not (a positive delta of unknown size); `earlier-censored` the reverse; `both censored` seeds "
             "decide nothing. Rankings are by mean rank over seeds with \"not reached\" ranked above every reached "
             "frame; `=` marks tied mean ranks.\n")
    L.append("Reading rule (E4): every headline number comes from the reference configuration. A cell that "
             "improves RA-DPR's standing is reported, never adopted. S10 and S11 are mandatory rows.\n")
    L.append("Metrics: " + "; ".join(f"`{m}` — {METRIC_NOTE.get(m, '')}" for m in metrics) + "\n")

    L.append("## Reference configuration (every sensitivity key at its default)\n")
    L.append("| arm | " + " | ".join(metrics) + " |")
    L.append("|---|" + "---|" * len(metrics))
    for arm in ALL_ARMS:
        cells = []
        for m in metrics:
            st = rep["reference"][arm][m]
            cells.append(("none reached" if st["reached"] == 0 else fmt(st["mean"], m))
                         + (f" ({st['reached']}/{st['n']} reached)" if 0 < st["reached"] < st["n"] else ""))
        L.append(f"| {arm} | " + " | ".join(cells) + " |")
    L.append("")
    L.append("Arm order under the reference: " + "; ".join(f"`{m}`: {rep['reference_order'][m]}" for m in metrics) + "\n")

    L.append("## Sensitivity cells (S1–S12)\n")
    L.append("| cell | key = value | arm | " + " | ".join(f"Δ {m}" for m in metrics) + " | ranking changed | RA-DPR standing | counter (ref → cell) |")
    L.append("|---|---|---|" + "---|" * len(metrics) + "---|---|---|")
    for c in rep["cells"]:
        if c["part_f"]:
            continue
        for arm in c["arms"]:
            d = c["deltas"][arm]
            changed = [m for m in metrics if c["rankings"][m]["changed"]]
            standing = {c["rankings"][m]["radpr_standing"] for m in metrics}
            standing_txt = "same" if standing == {"same"} else ", ".join(
                f"{m}: {c['rankings'][m]['radpr_standing']}" for m in metrics if c["rankings"][m]["radpr_standing"] != "same")
            counters = "; ".join(f"`{k}` {fmt(v['ref_mean'], k)} → {fmt(v['cell_mean'], k)}" for k, v in c["counters"][arm].items())
            tag = " **(mandatory)**" if c["mandatory"] else ""
            L.append(f"| {c['id']}{tag} | `{c['key']} = {c['value']}` | {arm} | "
                     + " | ".join(delta_cell_text(d[m], m) for m in metrics)
                     + f" | {', '.join(changed) if changed else 'no'} | {standing_txt} | {counters} |")
    L.append("")
    L.append("### Rankings per cell\n")
    L.append("| cell | metric | reference order | cell order | changed |")
    L.append("|---|---|---|---|---|")
    for c in rep["cells"]:
        if c["part_f"]:
            continue
        for m in metrics:
            r = c["rankings"][m]
            L.append(f"| {c['id']} | `{m}` | {r['ref_order']} | {r['cell_order']} | {'**yes**' if r['changed'] else 'no'} |")
    L.append("")
    zero = [c["id"] for c in rep["cells"] if not c["part_f"] and all(c["deltas"][a][m]["all_zero"] for a in c["arms"] for m in metrics)]
    L.append("Cells whose deltas are all zero on every metric and every affected arm (the ambiguity does not matter): "
             + (", ".join(zero) if zero else "none") + ".\n")
    L.append("Named counters: S5 `dcfr_rule1_bindings`, S8 `slot_inheritances` and `colouring_violations`, "
             "S10 `rotations_total`, S11 `control_bits_frac`. The other counters are this tool's choice of the number "
             "the switch should move.\n")

    L.append("## S13 — `topology_profile = paper` (a different network; no delta against the reference)\n")
    pf = rep.get("part_f")
    if pf is None:
        L.append("Not run.\n")
    else:
        L.append("| arm | profile | " + " | ".join(metrics) + " | nemcr_out_of_reach_frac | nemcr_stage_a |")
        L.append("|---|---|" + "---|" * (len(metrics) + 2))
        for arm in ALL_ARMS:
            for prof in ("shared", "paper"):
                st = pf["means"][prof][arm]
                L.append(f"| {arm} | {prof} | " + " | ".join(
                    ("none reached" if st[m]["reached"] == 0 else fmt(st[m]["mean"], m))
                    + (f" ({st[m]['reached']}/{st[m]['n']} reached)" if 0 < st[m]["reached"] < st[m]["n"] else "")
                    for m in metrics) + f" | {st['nemcr_out_of_reach_frac']} | {st['nemcr_stage_a']} |")
        L.append("")
        L.append("Arm order under `paper`: " + "; ".join(f"`{m}`: {pf['order'][m]}" for m in metrics) + "\n")
        L.append("### Falsification checklist, from summary columns\n")
        L.append("| # | claim | profile | test | holds on | fails on | undecided | verdict |")
        L.append("|---|---|---|---|---|---|---|---|")
        for r in pf["falsification"]:
            L.append(f"| {r['id']} | {r['claim']} | {r['profile']} | {r['test']} | {r['holds_on']} | {r['fails_on']} | "
                     f"{r['undecided']} | **{r['verdict']}** |")
        L.append("")
        L.append("F-1's undecided seeds (equal Jain) count toward HOLDS because the claim is \"not inferior\". "
                 "Our `dcfr` and `escfr` are our readings of Liu, and Urmonov's were theirs, so F-4 to F-6 test our "
                 "reading against theirs. `partition_frame` is censored at T_stop on most seeds (R8); "
                 "the `first_orphan_frame` row is the same claim under the other reading of partition.\n")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")


def cmd_report(args: argparse.Namespace) -> int:
    out = resolve(args.out)
    metrics = [m.strip() for m in args.metrics.split(",") if m.strip()]
    plan = read_json(out / "plan.json") if (out / "plan.json").is_file() else None
    ref = CellData(REFERENCE, out / REFERENCE.dirname)
    if not ref.rows:
        sys.exit(f"no reference results at {rel(out / REFERENCE.dirname / RESULTS)}; run first")
    seeds = ref.seeds()
    missing_ref = [a for a in ALL_ARMS if a not in ref.arms()]
    if missing_ref:
        sys.exit(f"reference lacks arms {missing_ref}; the reference must run every arm")
    man = read_json(ref.dir / MANIFEST)
    rep: dict = {
        "created": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"), "out": rel(out),
        "rho": man.get("rho"), "seeds": seeds, "sim_version": man.get("sim_version", ""),
        "git_commit": man.get("git_commit", ""), "metrics": metrics, "reference": {}, "reference_order": {},
        "cells": [], "missing_cells": [], "part_f": None,
    }
    fin = lambda xs: [x for x in xs if not math.isnan(x) and not math.isinf(x)]  # noqa: E731
    for arm in ALL_ARMS:
        rep["reference"][arm] = {}
        for m in metrics:
            vals = [ref.value(arm, s, m) for s in seeds]
            rep["reference"][arm][m] = {"mean": statistics.mean(fin(vals)) if fin(vals) else math.nan,
                                        "reached": len(fin(vals)), "n": len(seeds)}
    for m in metrics:
        rep["reference_order"][m] = ranking(ref, None, m, seeds, [])["ref_order"]

    delta_rows: list[dict] = []
    rank_rows: list[dict] = []
    counter_rows: list[dict] = []
    for cell in CELLS:
        if cell.key is None:
            continue
        data = CellData(cell, out / cell.dirname)
        arms = [a for a in (ALL_ARMS if (plan and plan.get("all_arms")) else cell.arms) if a in data.arms()]
        expected = list(ALL_ARMS if (plan and plan.get("all_arms")) else cell.arms)
        if not data.rows or data.seeds() != seeds or arms != expected:
            rep["missing_cells"].append({"id": cell.id, "label": cell.label, "have_arms": data.arms(),
                                         "have_seeds": len(data.seeds()), "want_arms": expected, "want_seeds": len(seeds)})
            print(f"[{cell.id}] {cell.label}: INCOMPLETE (arms {data.arms()} of {expected}, {len(data.seeds())}/{len(seeds)} seeds)")
            if not data.rows:
                continue
        entry = {"id": cell.id, "key": cell.key, "value": cell.value, "label": cell.label, "arms": arms,
                 "mandatory": cell.mandatory, "part_f": cell.part_f, "dir": rel(data.dir),
                 "deltas": {}, "rankings": {}, "counters": {}}
        if cell.part_f:
            means = {"shared": {}, "paper": {}}
            for prof, d in (("shared", ref), ("paper", data)):
                for arm in ALL_ARMS:
                    st = {}
                    for m in metrics:
                        vals = [d.value(arm, s, m) for s in seeds]
                        st[m] = {"mean": statistics.mean(fin(vals)) if fin(vals) else math.nan,
                                 "reached": len(fin(vals)), "n": len(seeds)}
                    for k in ("nemcr_out_of_reach_frac", "nemcr_stage_a"):
                        vals = fin([d.value(arm, s, k) for s in seeds])
                        st[k] = fmt(statistics.mean(vals), k) if vals else "n/a"
                    means[prof][arm] = st
            order = {m: ranking(data, None, m, seeds, [])["ref_order"] for m in metrics}
            rep["part_f"] = {"cell": cell.id, "means": means, "order": order,
                             "falsification": falsification(ref, data, seeds)}
            rep["cells"].append(entry)
            print(f"[{cell.id}] {cell.label}: {len(arms)} arms x {len(seeds)} seeds, reported separately")
            continue
        for arm in arms:
            entry["deltas"][arm] = {}
            for m in metrics:
                d = paired_delta(ref, data, arm, m, seeds)
                entry["deltas"][arm][m] = d
                delta_rows.append({"cell": cell.id, "key": cell.key, "value": cell.value, "arm": arm, "metric": m, **d})
            entry["counters"][arm] = {}
            for k in cell.counters:
                cr = counter_row(ref, data, arm, k, seeds)
                entry["counters"][arm][k] = cr
                counter_rows.append({"cell": cell.id, "arm": arm, "counter": k, "named_by_build_plan": cell.named, **cr})
        for m in metrics:
            r = ranking(ref, data, m, seeds, arms)
            entry["rankings"][m] = {k: v for k, v in r.items() if k not in ("ref_ranks", "cell_ranks")}
            rank_rows.append({"cell": cell.id, "key": cell.key, "value": cell.value, "metric": m, **entry["rankings"][m]})
        rep["cells"].append(entry)
        changed = [m for m in metrics if entry["rankings"][m]["changed"]]
        print(f"[{cell.id}] {cell.label}: arms {','.join(arms)}; " + "; ".join(
            f"{a} d{m} {delta_cell_text(entry['deltas'][a][m], m)}" for a in arms for m in metrics[1:3])
            + f"; ranking changed: {', '.join(changed) if changed else 'no'}")
    if rep["part_f"] is None:
        rep["part_f_falsification_shared_only"] = falsification(ref, None, seeds)

    write_csv(out / "sensitivity.csv",
              ["cell", "key", "value", "arm", "metric", "n_seeds", "n_numeric", "n_pos", "n_neg", "n_zero",
               "n_cell_later_censored", "n_cell_earlier_censored", "n_both_censored", "n_fnd_censored", "n_missing",
               "mean_delta", "median_delta", "sign", "all_zero", "ref_mean", "cell_mean", "ref_reached", "cell_reached"],
              delta_rows)
    write_csv(out / "rankings.csv",
              ["cell", "key", "value", "metric", "ref_order", "cell_order", "changed", "radpr_position_ref",
               "radpr_position_cell", "radpr_standing"], rank_rows)
    write_csv(out / "counters.csv", ["cell", "arm", "counter", "named_by_build_plan", "ref_mean", "cell_mean", "n_changed"],
              counter_rows)
    fals = rep["part_f"]["falsification"] if rep["part_f"] else rep["part_f_falsification_shared_only"]
    write_csv(out / "falsification.csv", ["id", "claim", "profile", "test", "holds_on", "fails_on", "undecided", "verdict"], fals)
    write_json(out / "sensitivity.json", rep)
    write_markdown(out / "sensitivity.md", rep, metrics)
    n_cells = sum(1 for c in rep["cells"] if not c["part_f"])
    print(f"report: {n_cells} sensitivity cells, {len(delta_rows)} delta rows, {len(rank_rows)} ranking rows, "
          f"{len(counter_rows)} counter rows, topology_profile=paper {'reported' if rep['part_f'] else 'not run'}, "
          f"{len(rep['missing_cells'])} incomplete cells -> {rel(out / 'sensitivity.md')}")
    return 1 if rep["missing_cells"] else 0


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="run the reference and every cell as compare.py campaigns")
    r.add_argument("--deployments", default="deployments", help="directory written by gen_deployments.py")
    r.add_argument("--rho", default="400")
    r.add_argument("--seeds", default="1..30", help="range a..b or comma list")
    r.add_argument("--out", default="out/sensitivity", help="matrix directory (relative to the repo root)")
    r.add_argument("--cells", default="", help="comma list of cell ids to run, e.g. S0,S10,S11 (default: all)")
    r.add_argument("--config", default="", help="JSON of extra non-sensitivity keys shared by every run (e.g. log_gaps)")
    r.add_argument("--all-arms", action="store_true", help="run every arm in every cell, not only the affected ones")
    r.add_argument("--force", action="store_true", help="re-run cells whose campaign is already complete")
    r.add_argument("--no-progress", action="store_true", help="do not open the progress window (tools/progress.py)")
    r.set_defaults(fn=cmd_run)

    p = sub.add_parser("report", help="pair every cell against the reference and write the tables")
    p.add_argument("--out", default="out/sensitivity", help="matrix directory written by run")
    p.add_argument("--metrics", default=",".join(DEFAULT_METRICS), help="summary columns to pair (all higher-is-better)")
    p.set_defaults(fn=cmd_report)

    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
