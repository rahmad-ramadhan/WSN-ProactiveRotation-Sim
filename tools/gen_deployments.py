#!/usr/bin/env python3
"""Write positions CSVs so every arm runs on the same node placements.

Draws match sim's own --seed: MT19937-64 seeded rho*1000 + seed, redrawn with
the seed advanced by 1000 until every node reaches the sink.

Output: <out>/r<rho>_s<seed:02d>.csv and <out>/index.csv

Usage:
  python tools/gen_deployments.py --rho 400 --seeds 1..30 --out deployments/
  python tools/gen_deployments.py --rho 400,600,800 --seeds 1,2,7 --out deployments/
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

from simlib import ROOT

N_DEFAULT = 400
R_MAX_DEFAULT = 80.0
SEED_STRIDE = 1000
MAX_ATTEMPTS = 100000


class MT19937_64:
    """64-bit Mersenne Twister, standard seeding (matches std::mt19937_64)."""

    NN, MM = 312, 156
    MATRIX_A = 0xB5026F5AA96619E9
    UM, LM = 0xFFFFFFFF80000000, 0x7FFFFFFF
    MASK = 0xFFFFFFFFFFFFFFFF

    def __init__(self, seed: int):
        mt = [0] * self.NN
        mt[0] = seed & self.MASK
        for i in range(1, self.NN):
            prev = mt[i - 1]
            mt[i] = (6364136223846793005 * (prev ^ (prev >> 62)) + i) & self.MASK
        self.mt = mt
        self.mti = self.NN

    def _twist(self) -> None:
        mt, NN, MM = self.mt, self.NN, self.MM
        for i in range(NN):
            x = (mt[i] & self.UM) | (mt[(i + 1) % NN] & self.LM)
            xA = x >> 1
            if x & 1:
                xA ^= self.MATRIX_A
            mt[i] = mt[(i + MM) % NN] ^ xA
        self.mti = 0

    def next_u64(self) -> int:
        if self.mti >= self.NN:
            self._twist()
        x = self.mt[self.mti]
        self.mti += 1
        x ^= (x >> 29) & 0x5555555555555555
        x ^= (x << 17) & 0x71D67FFFEDA60000
        x ^= (x << 37) & 0xFFF7EEE000000000
        x ^= x >> 43
        return x & self.MASK

    def next_unit(self) -> float:
        """Uniform in [0, 1): top 53 bits over 2^53."""
        return (self.next_u64() >> 11) * (1.0 / 9007199254740992.0)


def field_side(N: int, rho: int) -> float:
    """L = sqrt(N / rho) km, in metres."""
    return math.sqrt(N / rho) * 1000.0


def connected_to_sink(pos: list[tuple[float, float]], R_max: float) -> bool:
    """True if every sensor reaches the sink in G_c."""
    n = len(pos)
    r2 = R_max * R_max
    seen = [False] * n
    seen[0] = True
    stack = [0]
    reached = 1
    while stack:
        u = stack.pop()
        ux, uy = pos[u]
        for v in range(n):
            if not seen[v]:
                dx, dy = pos[v][0] - ux, pos[v][1] - uy
                if dx * dx + dy * dy <= r2:
                    seen[v] = True
                    reached += 1
                    stack.append(v)
    return reached == n


def draw_once(rng: MT19937_64, N: int, L: float) -> list[tuple[float, float]]:
    pos = [(L / 2.0, L / 2.0)]
    for _ in range(N):
        ux = rng.next_unit()
        uy = rng.next_unit()
        pos.append((L * ux, L * uy))
    return pos


def draw_deployment(rho: int, seed: int, N: int, R_max: float) -> tuple[list[tuple[float, float]], int]:
    L = field_side(N, rho)
    for attempt in range(MAX_ATTEMPTS):
        mt_seed = rho * 1000 + seed + SEED_STRIDE * attempt
        pos = draw_once(MT19937_64(mt_seed), N, L)
        if connected_to_sink(pos, R_max):
            return pos, attempt
    sys.exit(f"rho={rho} seed={seed}: rejected {MAX_ATTEMPTS} times; the field cannot be connected")


def deployment_filename(rho: int, seed: int) -> str:
    return f"r{rho}_s{seed:02d}.csv"


def write_deployment(path: Path, rho: int, seed: int, N: int, R_max: float,
                     pos: list[tuple[float, float]], rejections: int) -> None:
    L = field_side(N, rho)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# deployment rho={rho} seed={seed} N={N} L={L!r} R_max={R_max!r} "
                f"rejections={rejections} generator=gen_deployments.py\n")
        f.write("id,x,y\n")
        for i, (x, y) in enumerate(pos):
            f.write(f"{i},{x!r},{y!r}\n")


def read_deployment(path: Path) -> tuple[dict[str, str], list[tuple[int, float, float]]]:
    meta: dict[str, str] = {}
    rows: list[tuple[int, float, float]] = []
    header_seen = False
    with open(path, "r", encoding="utf-8", newline="") as f:
        for line in f:
            t = line.strip()
            if not t:
                continue
            if t.startswith("#"):
                for tok in t[1:].split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        meta[k] = v
                continue
            if not header_seen:
                if t != "id,x,y":
                    sys.exit(f"{path}: expected header id,x,y, got {t!r}")
                header_seen = True
                continue
            a, b, c = t.split(",")
            rows.append((int(a), float(b), float(c)))
    return meta, rows


def parse_seeds(text: str) -> list[int]:
    """'1..30' -> 1..30 inclusive; '1,2,7' -> [1, 2, 7]; forms can be mixed."""
    out: list[int] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if ".." in part:
            a, b = part.split("..", 1)
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    if not out:
        sys.exit("--seeds is empty")
    return out


def parse_rhos(text: str) -> list[int]:
    rhos = [int(p) for p in text.split(",") if p.strip()]
    for r in rhos:
        if r not in (400, 600, 800, 1000):
            sys.exit(f"rho={r} is not one of 400, 600, 800, 1000")
    return rhos


def resolve_out(text: str) -> Path:
    p = Path(text)
    return p if p.is_absolute() else ROOT / p


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rho", default="400", help="density or comma list: 400 | 400,600,800")
    ap.add_argument("--seeds", default="1..30", help="range a..b or comma list")
    ap.add_argument("--out", default="deployments", help="output directory (relative to the repo root)")
    ap.add_argument("--N", type=int, default=N_DEFAULT)
    ap.add_argument("--R-max", type=float, default=R_MAX_DEFAULT, dest="R_max")
    args = ap.parse_args()

    out = resolve_out(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rhos, seeds = parse_rhos(args.rho), parse_seeds(args.seeds)

    index_path = out / "index.csv"
    existing: dict[str, str] = {}
    if index_path.is_file():
        for line in index_path.read_text(encoding="utf-8").splitlines()[1:]:
            if line.strip():
                existing[line.split(",")[3]] = line
    per_rho: dict[int, list[int]] = {}
    for rho in rhos:
        for seed in seeds:
            pos, rej = draw_deployment(rho, seed, args.N, args.R_max)
            name = deployment_filename(rho, seed)
            write_deployment(out / name, rho, seed, args.N, args.R_max, pos, rej)
            existing[name] = f"{rho},{seed},{rej},{name}"
            per_rho.setdefault(rho, []).append(rej)
            print(f"{name}: rejections={rej}")
    with open(index_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("rho,seed,rejections,file\n")
        for line in sorted(existing.values(), key=lambda s: (int(s.split(",")[0]), int(s.split(",")[1]))):
            f.write(line + "\n")
    for rho, rejs in per_rho.items():
        print(f"rho={rho}: {len(rejs)} deployments, {sum(1 for r in rejs if r > 0)} needed a redraw, "
              f"{sum(rejs)} rejected draws in total")
    print(f"wrote {sum(len(v) for v in per_rho.values())} files and {index_path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
