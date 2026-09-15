#!/usr/bin/env python3
"""Compare arms across runs: one panel per metric, mean plus every run.

    python tools/plot_compare.py out/campaign [-o fig/]
"""
import argparse
import csv
import os
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# (column, axis label, lower-is-better)
METRICS = [
    ("fnd_frame",      "First death (frame)",        False),
    ("pdr_global",     "Delivery ratio",             False),
    ("mj_per_payload", "Energy (mJ / payload)",      True),
    ("jain_index_fnd", "Jain index at first death",  False),
    ("control_bits",   "Control bits",               True),
    ("t_50pct",        "Half the nodes dead (frame)", False),
]


def find_summaries(root):
    hits = []
    for dirpath, _dirnames, filenames in os.walk(root):
        if "summary.csv" in filenames:
            hits.append(os.path.join(dirpath, "summary.csv"))
    return sorted(hits)


def num(row, key):
    try:
        return float(row.get(key, ""))
    except (TypeError, ValueError):
        return float("nan")


def main():
    ap = argparse.ArgumentParser(description="Compare arms across runs.")
    ap.add_argument("root", help="directory holding run directories")
    ap.add_argument("-o", "--out", default=None, help="output directory (default: the root)")
    ap.add_argument("--label", default=None, help="use this config column as the arm label instead of `arm`")
    ap.add_argument("--by-dir", action="store_true",
                    help="label by run-directory name up to the last _s (use this when several "
                         "configurations share one arm, e.g. CF2M runs on the radpr arm)")
    args = ap.parse_args()

    paths = find_summaries(args.root)
    if not paths:
        sys.exit("no summary.csv found under %s" % args.root)

    by_arm = defaultdict(list)
    for p in paths:
        run_name = os.path.basename(os.path.dirname(p))
        with open(p, newline="") as fh:
            for row in csv.DictReader(fh):
                if args.by_dir:
                    label = run_name.rsplit("_s", 1)[0] if "_s" in run_name else run_name
                elif args.label:
                    label = row.get(args.label)
                else:
                    label = row.get("arm", "?")
                delivered = num(row, "delivered_total")
                energy = num(row, "energy_total")
                row["mj_per_payload"] = (energy * 1000.0 / delivered) if delivered > 0 else float("nan")
                by_arm[label or "?"].append(row)

    arms = sorted(by_arm)
    print("%d runs over %d arms: %s" % (len(paths), len(arms), ", ".join(arms)))

    fig, axes = plt.subplots(2, 3, figsize=(15, 8))
    fig.suptitle("arm comparison — %d runs from %s" % (len(paths), os.path.normpath(args.root)))

    for ax, (key, label, lower_better) in zip(axes.flat, METRICS):
        means, xs = [], list(range(len(arms)))
        for i, arm in enumerate(arms):
            vals = [num(r, key) if key != "mj_per_payload" else r["mj_per_payload"] for r in by_arm[arm]]
            vals = [v for v in vals if v == v]      # drop NaN
            means.append(sum(vals) / len(vals) if vals else float("nan"))
            ax.plot([i] * len(vals), vals, "o", ms=3, alpha=0.35, color="tab:blue", zorder=3)
        ax.bar(xs, means, color="tab:blue", alpha=0.35, zorder=2)
        ax.set_xticks(xs)
        ax.set_xticklabels(arms, rotation=30, ha="right", fontsize=8)
        ax.set_title(label + ("  (lower is better)" if lower_better else ""), fontsize=9)
        ax.grid(alpha=0.3, axis="y")
        if all(m != m for m in means):
            ax.text(0.5, 0.5, "not reached in any run\n(run longer with T_stop)",
                    transform=ax.transAxes, ha="center", va="center", fontsize=9, color="0.4")

    for ax in list(axes.flat)[len(METRICS):]:
        ax.axis("off")

    fig.tight_layout()
    out_dir = args.out or args.root
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, "compare.png")
    fig.savefig(dest, dpi=150)
    print(dest)


if __name__ == "__main__":
    main()
