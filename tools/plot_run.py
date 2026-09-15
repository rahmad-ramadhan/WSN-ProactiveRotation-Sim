#!/usr/bin/env python3
"""Plot one run's survival, delivery, energy and balance over time.

    python tools/plot_run.py out/my_run [-o fig/]
"""
import argparse
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_csv(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def col(rows, name, cast=float):
    out = []
    for r in rows:
        v = r.get(name, "")
        try:
            out.append(cast(v))
        except (TypeError, ValueError):
            out.append(float("nan"))
    return out


def cumulative(xs):
    total, out = 0.0, []
    for x in xs:
        total += 0.0 if x != x else x       # skip NaN
        out.append(total)
    return out


def main():
    ap = argparse.ArgumentParser(description="Plot one simulator run.")
    ap.add_argument("run", help="run directory containing frames.csv and summary.csv")
    ap.add_argument("-o", "--out", default=None, help="output directory (default: beside the run)")
    args = ap.parse_args()

    frames_path = os.path.join(args.run, "frames.csv")
    if not os.path.isfile(frames_path):
        sys.exit("no frames.csv in %s" % args.run)
    frames = read_csv(frames_path)
    if not frames:
        sys.exit("frames.csv is empty")

    summary_path = os.path.join(args.run, "summary.csv")
    summary = read_csv(summary_path)[0] if os.path.isfile(summary_path) else {}
    arm = summary.get("arm", "?")
    seed = summary.get("seed", "?")

    f = col(frames, "f")
    fnd = None
    try:
        fnd = float(summary.get("fnd_frame", ""))
    except (TypeError, ValueError):
        pass

    fig, axes = plt.subplots(2, 2, figsize=(11, 7))
    fig.suptitle("run: arm=%s  seed=%s  (%s)" % (arm, seed, os.path.basename(os.path.normpath(args.run))))

    def mark_fnd(ax):
        if fnd is not None and fnd == fnd and fnd > 0:
            ax.axvline(fnd, color="red", ls="--", lw=1.3, zorder=0)
            ax.annotate("first death", xy=(fnd, ax.get_ylim()[1]), xytext=(3, -10),
                        textcoords="offset points", color="red", fontsize=8, va="top")

    ax = axes[0][0]
    ax.plot(f, col(frames, "alive"), label="alive")
    ax.plot(f, col(frames, "routed"), label="routed to sink")
    ax.plot(f, col(frames, "orphan"), label="orphaned")
    ax.set_title("Survival")
    ax.set_xlabel("frame")
    ax.set_ylabel("nodes")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    mark_fnd(ax)

    ax = axes[0][1]
    ax.plot(f, cumulative(col(frames, "delivered")), label="delivered")
    ax.plot(f, cumulative(col(frames, "lost_orphan")), label="lost, orphaned")
    ax.plot(f, cumulative(col(frames, "lost_death")), label="lost, died holding")
    ax.set_title("Payloads, cumulative")
    ax.set_xlabel("frame")
    ax.set_ylabel("payloads")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    mark_fnd(ax)

    ax = axes[1][0]
    data = cumulative(col(frames, "energy_data"))
    ctrl = cumulative(col(frames, "energy_ctrl"))
    ax.plot(f, data, label="data")
    ax.plot(f, ctrl, label="control")
    ax.plot(f, [a + b for a, b in zip(data, ctrl)], label="total", ls=":")
    ax.set_title("Energy issued, cumulative")
    ax.set_xlabel("frame")
    ax.set_ylabel("joules")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    mark_fnd(ax)

    ax = axes[1][1]
    mean = col(frames, "residual_mean")
    sd = col(frames, "residual_sd")
    ax.plot(f, mean, label="mean residual")
    ax.plot(f, col(frames, "residual_min"), label="minimum residual")
    ax.fill_between(f, [m - s for m, s in zip(mean, sd)], [m + s for m, s in zip(mean, sd)],
                    alpha=0.2, label="+/- 1 sd")
    ax.set_title("Residual energy")
    ax.set_xlabel("frame")
    ax.set_ylabel("joules")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    mark_fnd(ax)

    fig.tight_layout()
    out_dir = args.out or os.path.dirname(os.path.normpath(args.run)) or "."
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, os.path.basename(os.path.normpath(args.run)) + "_run.png")
    fig.savefig(dest, dpi=150)
    print(dest)


if __name__ == "__main__":
    main()
