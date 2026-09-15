# WSN-ProactiveRotation-Sim

A discrete-event simulator for TDMA wireless sensor networks under node death.
Nodes sense once per frame, forward what they hold up a routing tree to the sink
in slot order, and are charged for exactly the bits they move; when a node's
battery reaches zero it dies, its children are orphaned, and the routing
algorithm has to cope. The simulator exists to compare those algorithms — how
long the network lives, how much data arrives, how evenly the energy is spent,
and what the control traffic costs.

Six algorithms ship with it, from a do-nothing baseline to two published
cost-function routers. Adding a seventh means one file and one table row.

Written in C++20 with no third-party dependencies. The plotting scripts need
Python 3 and matplotlib; nothing else does.

## Build

Linux / macOS:

```sh
./BUILD_LINUX.sh          # or: cmake -S . -B build && cmake --build build
```

Windows:

```bat
BUILD_WINDOWS.bat
```

Either way the binary lands at `build/sim` (`build/sim.exe` on Windows).
CMake 3.20 or newer and any C++20 compiler will do.

## Run

```sh
build/sim --config configs/cf2m.json --positions example/deployment.csv --seed 1 --out out/example
```

```
sim 1.0.0: run radpr_rho400_s1 arm=radpr rho=400 seed=1 hash=4bbc44ae3d85cf1d -> out/example/
setup: profile=shared positions=example/deployment.csv rejections=0 K=21 depth_max=13 mean_deg_gc=7.33666 max_deg_gi=39 P_max=0.107075 (node 180) E_0=642.453 I6=ok
run: frames=19565 stop=partition fnd=19468 partition=19565 adt=not reached generated=7825799 delivered=7813880 lost_orphan=10049 lost_death=216 held=1654 pdr=0.9984769606272791 energy=37644.71566053247 i14_checked=4518 max_B=428 trace=off/0 wall=0.98329s
```

`example/deployment.csv` is one fixed deployment of 400 nodes, shipped so this
command gives everyone the same figures. With `--positions` the seed is only a
label: the file *is* the deployment. Drop the flag to draw a fresh one from the
seed instead.

Roughly a second per run. Exit 0 means it completed; 2 is a configuration
error and 3 an invariant failure, each with one line on stderr.

| Flag | Meaning |
|---|---|
| `--config <file.json>` | required; a JSON object of config keys, and `"arm"` must be one of them |
| `--seed <u64>` | deployment seed; overrides `"seed"` in the file |
| `--out <dir>` | required; created if missing |
| `--positions <csv>` | a positions file (`id,x,y`) used instead of the random draw |
| `--fast` | skip the O(N²) invariant checks |

The two console lines are a convenience. The record is `summary.csv`.

## The algorithms

| arm | what it does | source |
|---|---|---|
| `static` | no control and no re-evaluation; orphans stay orphaned and keep transmitting into nothing | baseline |
| `nemcr` | backup-parent recovery with slot inheritance: each node fixes a takeover child at setup and uses it when the parent dies | Urmonov & Kim 2018 |
| `dcfr` | cost-function routing on residual energy **and** forwarding rate, re-evaluated every frame | Liu et al. 2012 |
| `escfr` | the same router with the rate term off — energy only | Liu et al. 2012 |
| `fa` | flow-augmentation path cost: a power law in link energy and residual fraction, summed along the path | Chang & Tassiulas 2000 |
| `radpr` | proactive rotation: every node re-scores its parent against its neighbours every `M` frames and switches when a candidate wins by more than the margin `Γ`; recovers through a stored backup, a fresh scan, or a ParentRequest | this project |

Two of the shipped configs are named candidates rather than bare arms:

- `configs/fa.json` — **F2**: the `fa` cost at exponent 2, re-evaluated every frame.
- `configs/cf2m.json` — **CF2M**: the same cost on the `radpr` arm at a 20-frame
  cadence with zero margin, detaching as soon as a node loses its route. It runs
  on the `radpr` arm, so its `summary.csv` reports `arm=radpr`.

## Configuration

A config file is a JSON object. Everything except `arm` and `seed` has a
default, so the smallest useful file is three lines:

```json
{
  "arm": "escfr"
}
```

`build/sim --help` prints every key with its type, default and permitted
values. `configs/` holds one minimal file per arm.

A few worth knowing: `rho` (node density), `N` (nodes), `T_stop` (frame cap,
default 30000), `M` (rotation/broadcast period, default 20), `Gamma` (switching
margin), and `control_cost_model` (whether control traffic is charged).

## Output

Every run writes these into `--out`:

| file | contents |
|---|---|
| `summary.csv` | one header line and one row: the whole run as ~125 columns — lifetime, delivery, balance, energy, control and per-arm counters |
| `frames.csv` | one row per frame: nodes alive, routed and orphaned, payloads delivered and lost, energy issued, residual energy spread |
| `nodes.csv` | one row per node: death frame, final energy, parent changes, frames spent orphaned |
| `topology.csv` | the deployment and the tree at setup: position, depth, slot, parent, degrees |
| `config.json` | the fully-resolved configuration, defaults included, for reproducing the run |

Some arms write more: `rotations.csv`, `recoveries.csv` and `gaps.csv` for
`radpr`, `nemcr_recovery.csv` and `nemcr_links.csv` for `nemcr`,
`dcfr_decisions.csv` for the Liu arms.

## Plotting

Three scripts turn a run directory into a PNG. They need `matplotlib`
(`pip install matplotlib`) and nothing else.

**One run, four panels** — survival, payloads, energy and residual-energy
spread, with the first death marked. This is the fastest way to see how a run
actually unfolded:

```sh
python tools/plot_run.py out/example
```

**The routing tree over the deployment** — nodes coloured by depth, edges to
parents, sink starred. Add `--final` for a second panel showing the run's end
state, where the colour becomes each node's remaining energy: circles are still
routed to the sink, squares are alive with no route left, bold red crosses are
dead, and the sink is a black star.
On the shipped example the two panels together show the energy hole — the nodes
that died are the ones ringing the sink, while the far field is still near-full
but cut off:

```sh
python tools/plot_topology.py out/example --final
```

Add `--detail` instead for a seven-panel, 300 dpi `*_topology_detail.png`: node
ids, depth at setup and at the end (the tree on the last frame before the first
death, replayed from `rotations.csv` and `recoveries.csv`), residual energy when
the run stopped, death frames, and the TDMA slot colouring (slot number on every
node, nodes per slot, and a check that no two nodes within `R_int` share a
slot):

```sh
python tools/plot_topology.py out/example --detail -o fig
```

**Arms side by side** — walks a directory of runs, groups them by arm, and plots
first death, delivery ratio, energy per payload, Jain index, control bits and
time to half the network, with every individual run drawn as a dot so the spread
is visible:

```sh
python tools/plot_compare.py out/campaign
python tools/plot_compare.py out/campaign --by-dir     # label by directory, not by arm
```

Use `--by-dir` whenever several configurations share one arm — CF2M runs on the
`radpr` arm, so without it CF2M and RA-DPR are averaged together. All three take
`-o <dir>` to choose where the PNG goes.

## Comparing arms properly

A single seed proves nothing. To compare arms you want the same deployments
given to each one:

```sh
python tools/gen_deployments.py --rho 400 --seeds 1..30 --out deployments/
python tools/compare.py campaign --arms static,nemcr,escfr,fa --deployments deployments/ --out out/campaign
python tools/plot_compare.py out/campaign
```

`gen_deployments.py` writes positions files; every arm then runs on identical
node placements, so randomness leaves the comparison entirely.
`tools/sensitivity.py` sweeps one config key at a time over a fixed deployment
set. Both print `--help`.

## Layout

```
src/          the simulator
  core/       config, JSON, RNG, assertions
  model/      deployment, graph, energy, calibration
  sim/        the frame loop, setup, precedence, variant dispatch
  arms/       one file per algorithm, plus the registry that lists them
  io/         the CSV writers
configs/      one minimal config per arm, plus the two named candidates
example/      one fixed 400-node deployment, so the quickstart is reproducible
tools/        campaign drivers and the plotting scripts
```

Adding an arm: write `src/arms/algo_<name>.cpp` implementing `Algorithm`,
declare its factory in `src/arms/registry.cpp`, and add one row to `arm_table()`.
The registry is the only place that knows the arm exists.

## Notes

Distances, energies and rates are in metres, joules and payloads per frame. The
energy model charges every transmission through a single ledger, so the totals
in `summary.csv` are exact rather than estimated.

The `--positions` loader treats a disconnected deployment as a hard error rather
than redrawing it: the generator that wrote the file was supposed to apply the
rejection, and silently redrawing would bias the comparison.
