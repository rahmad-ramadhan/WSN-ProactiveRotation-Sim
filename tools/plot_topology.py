#!/usr/bin/env python3
"""Draw the routing tree over the deployment.

    python tools/plot_topology.py out/my_run [--final] [--depth-lines] [-o fig/]
    python tools/plot_topology.py out/my_run --detail [-o fig/]

Left: the setup tree coloured by depth. Right (--final): the tree at stop,
coloured by residual energy; squares are alive with no route, red crosses are dead.
--detail writes *_topology_detail.png: node ids, depth at setup and at the end,
residual energy when the run stopped, death frames, and the TDMA slot colouring.
"""
import argparse
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

# recoveries.csv events that do not move a parent
NO_PARENT_CHANGE = {"request_sent", "request_failed"}


def read_csv(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def as_int(text, default=None):
    try:
        return int(text)
    except (TypeError, ValueError):
        return default


def depth_lines(ax, pos, depth, label="depth k begins"):
    """Contour the smoothed depth field at k - 0.5, so the line labelled k is where
    depth k begins. Hop depth is not radial far from the sink, so rings would not fit.
    """
    ids = [i for i in pos if depth.get(i) is not None and depth[i] >= 0]
    if len(ids) < 3:
        return None
    x = np.array([pos[i][0] for i in ids])
    y = np.array([pos[i][1] for i in ids])
    d = np.array([depth[i] for i in ids], dtype=float)
    if d.max() < 1:
        return None
    w_x, w_y = (x.max() - x.min()) or 1.0, (y.max() - y.min()) or 1.0
    sigma = 0.8 * (w_x * w_y / len(ids)) ** 0.5
    gx, gy = np.meshgrid(np.linspace(x.min(), x.max(), 160), np.linspace(y.min(), y.max(), 160))
    num, den = np.zeros_like(gx), np.zeros_like(gx)
    for xi, yi, di in zip(x, y, d):
        k = np.exp(-((gx - xi) ** 2 + (gy - yi) ** 2) / (2 * sigma ** 2))
        num += k * di
        den += k
    field = num / np.maximum(den, 1e-300)
    levels = np.arange(0.5, d.max(), 1.0)
    cs = ax.contour(gx, gy, field, levels=levels, colors="0.15", linewidths=0.7,
                    linestyles="--", alpha=0.7, zorder=1.5)
    ax.clabel(cs, fmt=lambda v: "%d" % round(v + 0.5), fontsize=6, inline=True)
    return Line2D([], [], ls="--", lw=0.7, color="0.15", alpha=0.7, label=label)


def draw(ax, pos, parents, values, dead, title, bar_label, vlim=None, depth=None,
         labels=None, ring=(), cmap="viridis", title_size=10):
    xs = [pos[i][0] for i in pos]
    ys = [pos[i][1] for i in pos]
    vmin, vmax = vlim if vlim else (None, None)

    for node, par in parents.items():
        if par is None or par < 0 or par not in pos or node not in pos:
            continue
        x0, y0 = pos[node]
        x1, y1 = pos[par]
        ax.plot([x0, x1], [y0, y1], "-", lw=0.4, color="0.65", zorder=1)

    loose = sorted(i for i in pos
                   if i != 0 and i not in dead and (parents.get(i) is None or parents.get(i) < 0))
    live = sorted(i for i in pos if i != 0 and i not in dead and i not in loose)
    gone = sorted(i for i in pos if i != 0 and i in dead)

    handles = []
    sc = None
    if gone:
        ax.scatter([pos[i][0] for i in gone], [pos[i][1] for i in gone],
                   s=16, c="red", marker="x", linewidths=0.9, zorder=5)
        handles.append(Line2D([], [], ls="", marker="x", color="red", ms=5,
                              markeredgewidth=0.9, label="dead"))
    if loose:
        sc = ax.scatter([pos[i][0] for i in loose], [pos[i][1] for i in loose],
                        s=14, c=[values.get(i, 0.0) for i in loose], cmap=cmap,
                        marker="s", vmin=vmin, vmax=vmax, zorder=2)
        handles.append(Line2D([], [], ls="", marker="s", color="0.45", ms=4.5, label="alive, no route"))
    if live:
        sc = ax.scatter([pos[i][0] for i in live], [pos[i][1] for i in live],
                        s=14, c=[values.get(i, 0.0) for i in live], cmap=cmap,
                        marker="o", vmin=vmin, vmax=vmax, zorder=3)
        handles.append(Line2D([], [], ls="", marker="o", color="0.45", ms=4.5, label="alive, routed"))
    ringed = [i for i in ring if i in pos]
    if ringed:
        ax.scatter([pos[i][0] for i in ringed], [pos[i][1] for i in ringed], s=60,
                   facecolors="none", edgecolors="red", linewidths=0.9, zorder=4)
        handles.append(Line2D([], [], ls="", marker="o", mfc="none", mec="red", ms=7,
                              label="dies before stop"))
    if sc is not None:
        cb = ax.figure.colorbar(sc, ax=ax, fraction=0.046, pad=0.04)
        cb.set_label(bar_label, fontsize=8)
        cb.ax.tick_params(labelsize=7)
    if 0 in pos:
        ax.scatter([pos[0][0]], [pos[0][1]], s=170, marker="*", c="black",
                   edgecolors="white", linewidths=0.6, zorder=6)
        handles.append(Line2D([], [], ls="", marker="*", color="black", ms=11, label="sink"))

    if depth:
        line = depth_lines(ax, pos, depth, "setup depth k begins" if labels else "depth k begins")
        if line is not None:
            handles.append(line)

    for i, text in (labels or {}).items():
        if i in pos:
            ax.annotate(text, pos[i], textcoords="offset points", xytext=(2.5, 2.0),
                        fontsize=3.2, color="red" if i in dead else "0.25", zorder=7)

    ax.set_title(title, fontsize=title_size)
    ax.set_aspect("equal")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    ax.legend(handles=handles, fontsize=8, loc="upper right", framealpha=0.9)
    ax.grid(alpha=0.25)
    pad = 0.02 * (max(xs) - min(xs) or 1)
    ax.set_xlim(min(xs) - pad, max(xs) + pad)
    ax.set_ylim(min(ys) - pad, max(ys) + pad)


def tree_depths(parents, dead):
    """Hops to the sink along `parents`; None where the chain breaks or meets a dead node."""
    memo = {0: 0}
    for start in parents:
        chain, i = [], start
        while i not in memo:
            if i in dead or parents.get(i, -1) < 0 or i in chain:
                break
            chain.append(i)
            i = parents[i]
        d = memo.get(i) if i in memo else None
        if i not in memo:
            memo[i] = None
        for j in reversed(chain):
            d = None if d is None else d + 1
            memo[j] = d
    return {i: memo.get(i) for i in parents}


def parents_at(run, setup_parents, frame):
    """Replay rotations.csv and recoveries.csv onto the setup tree up to `frame`.
    None when the run has no such logs (only the radpr arm writes them)."""
    events = []
    for name in ("rotations.csv", "recoveries.csv"):
        path = os.path.join(run, name)
        if not os.path.isfile(path):
            return None
        for r in read_csv(path):
            if r.get("event") in NO_PARENT_CHANGE:
                continue
            events.append((int(r["f"]), int(r["id"]), int(r["new"])))
    events.sort(key=lambda e: e[0])
    parents = dict(setup_parents)
    for f, i, new in events:
        if f > frame:
            break
        parents[i] = new
    return parents


def read_nodes(run):
    path = os.path.join(run, "nodes.csv")
    if not os.path.isfile(path):
        sys.exit("this plot needs nodes.csv, which %s does not have" % run)
    final_parents, death, energy = {0: -1}, {}, {}
    for r in read_csv(path):
        i = int(r["id"])
        final_parents[i] = as_int(r.get("final_parent"), -1)
        # nodes.csv writes death_frame 0 for a living node.
        if as_int(r.get("death_frame"), 0) > 0:
            death[i] = int(r["death_frame"])
        try:
            energy[i] = max(float(r["E_final"]), 0.0)
        except (TypeError, ValueError):
            energy[i] = 0.0
    return final_parents, death, energy


def slot_colours(k):
    """k colours with hues spread by the golden ratio, so consecutive slots differ."""
    import colorsys
    return [colorsys.hls_to_rgb((s * 0.618034) % 1.0, (0.42, 0.58, 0.72)[s % 3], 0.75) for s in range(k)]


def slot_panels(ax_map, ax_bar, pos, slot, gi, summary):
    """(f) the slot colouring over the deployment, (g) nodes per slot and the reuse check."""
    sensors = sorted(i for i in pos if i and slot.get(i, 0) > 0)
    xy = np.array([pos[i] for i in sensors])
    s = np.array([slot[i] for i in sensors])
    k = int(s.max())
    colours = slot_colours(k)
    try:
        r_int = float(summary["R_int"])
    except (KeyError, TypeError, ValueError):
        r_int = None

    dist = np.hypot(xy[:, None, 0] - xy[None, :, 0], xy[:, None, 1] - xy[None, :, 1])
    same = (s[:, None] == s[None, :]) & ~np.eye(len(sensors), dtype=bool)
    near = np.where(same, dist, np.inf)
    a, b = np.unravel_index(np.argmin(near), near.shape)
    closest = near[a, b]
    clashes = int(np.sum(np.triu(same & (dist <= r_int), 1))) if r_int else None

    handles = []
    ax_map.scatter(xy[:, 0], xy[:, 1], s=46, c=[colours[v - 1] for v in s],
                   edgecolors="white", linewidths=0.3, zorder=3)
    for (x, y), v in zip(xy, s):
        r, g, bl = colours[v - 1]
        ink = "black" if 0.299 * r + 0.587 * g + 0.114 * bl > 0.55 else "white"
        ax_map.text(x, y, str(v), ha="center", va="center", fontsize=3.4, color=ink, zorder=4)
    if r_int:
        ax_map.add_patch(plt.Circle(xy[a], r_int, fill=False, ls="--", lw=0.9, color="0.2", zorder=5))
        handles.append(Line2D([], [], ls="--", lw=0.9, color="0.2", label="R_int = %.0f m around node %d"
                              % (r_int, sensors[a])))
    ax_map.plot([xy[a, 0], xy[b, 0]], [xy[a, 1], xy[b, 1]], "-", lw=1.2, color="black", zorder=5)
    ax_map.annotate("%.2f m" % closest, ((xy[a, 0] + xy[b, 0]) / 2, (xy[a, 1] + xy[b, 1]) / 2),
                    textcoords="offset points", xytext=(4, 4), fontsize=8, zorder=6,
                    bbox=dict(facecolor="white", edgecolor="none", alpha=0.85, pad=1))
    handles.append(Line2D([], [], lw=1.2, color="black", label="closest same-slot pair (%d, %d)"
                          % (sensors[a], sensors[b])))
    if 0 in pos:
        ax_map.scatter([pos[0][0]], [pos[0][1]], s=170, marker="*", c="black",
                       edgecolors="white", linewidths=0.6, zorder=6)
        handles.append(Line2D([], [], ls="", marker="*", color="black", ms=11, label="sink (no slot)"))
    ax_map.set_title("(f) TDMA slot colouring at setup, fixed for the run\n"
                     "colour and label: slot, 1 to %d · no two nodes within R_int share a slot" % k, fontsize=11)
    ax_map.set_aspect("equal")
    ax_map.set_xlabel("x (m)")
    ax_map.set_ylabel("y (m)")
    ax_map.legend(handles=handles, fontsize=8, loc="upper right", framealpha=0.9)
    ax_map.grid(alpha=0.25)
    pad = 0.02 * (xy[:, 0].max() - xy[:, 0].min() or 1)
    ax_map.set_xlim(min(xy[:, 0].min(), pos[0][0]) - pad, max(xy[:, 0].max(), pos[0][0]) + pad)
    ax_map.set_ylim(min(xy[:, 1].min(), pos[0][1]) - pad, max(xy[:, 1].max(), pos[0][1]) + pad)

    counts = [int(np.sum(s == v)) for v in range(1, k + 1)]
    ax_bar.bar(range(1, k + 1), counts, color=colours, edgecolor="white", linewidth=1.0)
    ax_bar.set_xticks(range(1, k + 1))
    ax_bar.tick_params(axis="x", labelsize=8)
    ax_bar.set_xlim(0.3, k + 0.7)
    ax_bar.set_ylim(0, max(counts) * 1.6)
    ax_bar.set_xlabel("slot")
    ax_bar.set_ylabel("sensor nodes")
    ax_bar.grid(alpha=0.25, axis="y")
    delta = max((d for i, d in gi.items() if i and d is not None), default=None)
    rows = ["K = %d slots for %d sensor nodes (sink excluded)" % (k, len(sensors))]
    if r_int:
        rows.append("same-slot pairs within R_int = %.0f m: %d" % (r_int, clashes))
    rows.append("closest same-slot pair: nodes %d and %d, %.2f m apart" % (sensors[a], sensors[b], closest))
    if delta is not None:
        rows.append("max interference degree = %d, so greedy colouring needs at most %d slots" % (delta, delta + 1))
    ax_bar.text(0.03, 0.97, "\n".join(rows), transform=ax_bar.transAxes, va="top", fontsize=9,
                bbox=dict(facecolor="white", edgecolor="0.6", alpha=0.95))
    ax_bar.set_title("(g) Sensor nodes per slot, and the reuse check\n"
                     "same colours as (f)", fontsize=11)



def detail(run, pos, parents, depth, slot, gi, summary, head):
    final_parents, death, energy = read_nodes(run)
    dead = set(death)
    stop = as_int(summary.get("stop_frame"))
    fnd = as_int(summary.get("fnd_frame"))
    try:
        E_0 = float(summary["E_0"])
    except (KeyError, TypeError, ValueError):
        E_0 = max(energy.values()) if energy else 1.0

    end_frame = fnd - 1 if fnd else stop
    end_parents = parents_at(run, parents, end_frame) if end_frame else None
    if end_parents is None:
        end_frame, end_parents = stop, final_parents
        end_note = "at stop (no parent-change logs to replay)"
    else:
        if stop and parents_at(run, parents, stop) != final_parents:
            print("warning: replayed parents at stop differ from nodes.csv", file=sys.stderr)
        end_note = "the last frame before the first death" if fnd else "at stop"
    dead_at_end = {i for i, f in death.items() if f <= (end_frame or 0)}
    end_depth = tree_depths(end_parents, dead_at_end)
    routed_end = sum(1 for i, d in end_depth.items() if i and d is not None)

    ids = {i: str(i) for i in pos if i}
    setup_vals = [d for i, d in depth.items() if i and d >= 0]
    end_vals = [d for i, d in end_depth.items() if i and d is not None]
    d_max = max(setup_vals + end_vals) if end_vals else max(setup_vals)

    fig = plt.figure(figsize=(30, 15))
    gs = fig.add_gridspec(2, 12, height_ratios=[1.0, 0.82])
    ax_a, ax_b, ax_c, ax_f = (fig.add_subplot(gs[0, 3 * k:3 * k + 3]) for k in range(4))
    ax_d, ax_e, ax_g = (fig.add_subplot(gs[1, 4 * k:4 * k + 4]) for k in range(3))

    draw(ax_a, pos, parents, depth, set(),
         "(a) Tree at setup\ncolour: depth at setup · label: node id",
         "depth at setup (hops)", (0, max(setup_vals)), depth=depth, labels=ids, title_size=11)

    draw(ax_b, pos, end_parents, {i: d for i, d in end_depth.items() if d is not None},
         dead_at_end,
         "(b) Tree at frame %s, %s\ncolour: depth at end · label: node id · %d routed"
         % (end_frame, end_note, routed_end),
         "depth at end (hops)", (0, max(end_vals) if end_vals else 1),
         labels=ids, ring=sorted(dead - dead_at_end), title_size=11)

    stop_labels = {i: ("%d @%d" % (i, death[i]) if i in dead else str(i)) for i in ids}
    detached = sum(1 for i in pos if i and i not in dead and final_parents.get(i, -1) < 0)
    draw(ax_c, pos, final_parents, energy, dead,
         "(c) At stop, frame %s (%s): %d dead, %d alive with no route\n"
         "colour: residual energy · label: node id, dead: id @death frame"
         % (stop, summary.get("stop_reason", "?"), len(dead), detached),
         "residual energy at stop (J)", (0.0, E_0), depth=depth, labels=stop_labels, title_size=11)

    both = [i for i in pos if i and depth.get(i, -1) >= 0 and end_depth.get(i) is not None]
    rng = np.random.default_rng(0)
    jitter = {i: rng.uniform(-0.3, 0.3) for i in both}
    alive_pts = [i for i in both if i not in dead]
    sc = ax_d.scatter([depth[i] + jitter[i] for i in alive_pts], [end_depth[i] for i in alive_pts],
                      c=[energy[i] for i in alive_pts], cmap="viridis", vmin=0.0, vmax=E_0,
                      s=14, zorder=3)
    gone = [i for i in both if i in dead]
    ax_d.scatter([depth[i] + jitter[i] for i in gone], [end_depth[i] for i in gone],
                 c="red", marker="x", s=30, linewidths=1.1, zorder=4)
    if gone:
        rows = ["dead: id  setup→end depth  @death frame"]
        rows += ["%5d   %d → %d   @%d" % (i, depth[i], end_depth[i], death[i])
                 for i in sorted(gone, key=lambda n: death[n])]
        ax_d.text(0.03, 0.97, "\n".join(rows), transform=ax_d.transAxes, va="top",
                  family="monospace", fontsize=8, color="red",
                  bbox=dict(facecolor="white", edgecolor="red", alpha=0.9))
    ax_d.plot([0, d_max], [0, d_max], "--", color="0.4", lw=0.8, zorder=1)
    ax_d.set_xlim(-0.8, max(setup_vals) + 0.8)
    ax_d.set_ylim(-1, d_max + 1)
    cb = fig.colorbar(sc, ax=ax_d, fraction=0.046, pad=0.02)
    cb.set_label("residual energy at stop (J)", fontsize=8)
    cb.ax.tick_params(labelsize=7)
    ax_d.set_title("(d) Depth at setup against depth at end, one dot per node\n"
                   "colour: residual energy at stop · red cross: dead · dashed: unchanged depth",
                   fontsize=11)
    ax_d.set_xlabel("depth at setup (hops, jittered)")
    ax_d.set_ylabel("depth at end, frame %s (hops)" % end_frame)
    ax_d.grid(alpha=0.25)

    bins = np.arange(-0.5, d_max + 1.5, 1.0)
    ax_e.hist(setup_vals, bins=bins, histtype="stepfilled", color="tab:blue", alpha=0.35,
              label="depth at setup (%d nodes)" % len(setup_vals))
    ax_e.hist(end_vals, bins=bins, histtype="step", color="tab:orange", lw=1.6,
              label="depth at end (%d routed)" % len(end_vals))
    dead_setup = [depth[i] for i in dead if depth.get(i, -1) >= 0]
    if dead_setup:
        ax_e.hist(dead_setup, bins=bins, histtype="stepfilled", color="red", alpha=0.8,
                  label="dead at stop, by depth at setup (%d)" % len(dead_setup))
    ax_r = ax_e.twinx()
    means = [(k, np.mean([energy[i] for i in pos if i and depth.get(i) == k]))
             for k in sorted(set(setup_vals))]
    ax_r.plot([k for k, _ in means], [m for _, m in means], "o-", color="tab:green", ms=4, lw=1.0)
    ax_r.set_ylim(0, 1.08 * E_0)
    ax_r.set_ylabel("mean residual energy at stop by depth at setup (J)", color="tab:green", fontsize=9)
    ax_r.tick_params(axis="y", colors="tab:green", labelsize=8)
    lines = ax_e.get_legend_handles_labels()
    ax_e.legend(lines[0] + [Line2D([], [], marker="o", color="tab:green", ms=4, lw=1.0)],
                lines[1] + ["mean residual energy at stop (right axis)"], fontsize=8, loc="center right")
    ax_e.set_title("(e) Nodes per depth, at setup and at end\n"
                   "blue: setup · orange: end · red: dead · green: mean energy left",
                   fontsize=11)
    ax_e.set_xlabel("depth (hops)")
    ax_e.set_ylabel("nodes")
    ax_e.grid(alpha=0.25)

    slot_panels(ax_f, ax_g, pos, slot, gi, summary)

    fig.suptitle("%s — routing tree in detail" % head, fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.985))
    return fig


def main():
    ap = argparse.ArgumentParser(description="Draw the routing tree.")
    ap.add_argument("run", help="run directory containing topology.csv")
    ap.add_argument("--final", action="store_true", help="add the tree as it stood at the stop frame")
    ap.add_argument("-o", "--out", default=None, help="output directory (default: beside the run)")
    ap.add_argument("--detail", action="store_true",
                    help="write *_topology_detail.png at 300 dpi: ids, depth at setup and at the end,"
                         " residual energy at stop, death frames, and the TDMA slot colouring")
    ap.add_argument("--depth-lines", action="store_true",
                    help="draw the setup tree's depth boundaries on every panel")
    args = ap.parse_args()

    topo_path = os.path.join(args.run, "topology.csv")
    if not os.path.isfile(topo_path):
        sys.exit("no topology.csv in %s" % args.run)

    pos, parents, depth, slot, gi = {}, {}, {}, {}, {}
    for r in read_csv(topo_path):
        i = int(r["id"])
        pos[i] = (float(r["x"]), float(r["y"]))
        parents[i] = int(r["parent"])
        depth[i] = int(r["depth"])
        slot[i] = as_int(r.get("slot"), 0)
        gi[i] = as_int(r.get("deg_gi"))

    summary_path = os.path.join(args.run, "summary.csv")
    summary = read_csv(summary_path)[0] if os.path.isfile(summary_path) else {}
    head = "arm=%s seed=%s" % (summary.get("arm", "?"), summary.get("seed", "?"))

    if args.detail:
        fig = detail(args.run, pos, parents, depth, slot, gi, summary, head)
    else:
        panels = 2 if args.final else 1
        fig, axes = plt.subplots(1, panels, figsize=(7.5 * panels, 7))
        axes = [axes] if panels == 1 else list(axes)

        lines = depth if args.depth_lines else None
        dvals = [d for d in depth.values() if d >= 0]
        draw(axes[0], pos, parents, depth, set(), "%s — tree at setup" % head,
             "depth", (min(dvals), max(dvals)) if dvals else None, depth=lines)

        if args.final:
            final_parents, death, energy = read_nodes(args.run)
            dead = set(death)
            try:
                E_0 = float(summary.get("E_0", ""))
            except (TypeError, ValueError):
                alive_e = [energy[i] for i in energy if i not in dead]
                E_0 = max(alive_e) if alive_e else 1.0
            detached = sum(1 for i in pos
                           if i != 0 and i not in dead and final_parents.get(i, -1) < 0)
            draw(axes[1], pos, final_parents, energy, dead,
                 "%s — at stop (frame %s): %d dead, %d alive with no route"
                 % (head, summary.get("stop_frame", "?"), len(dead), detached),
                 "residual energy (J)", (0.0, E_0), depth=lines)
        fig.tight_layout()

    out_dir = args.out or os.path.dirname(os.path.normpath(args.run)) or "."
    os.makedirs(out_dir, exist_ok=True)
    suffix = "_topology_detail.png" if args.detail else "_topology.png"
    dest = os.path.join(out_dir, os.path.basename(os.path.normpath(args.run)) + suffix)
    fig.savefig(dest, dpi=300 if args.detail else 150)
    print(dest)


if __name__ == "__main__":
    main()
