#!/usr/bin/env python3
"""Reader for trace.csv (f,id,parent,sent,recv,B,E,charge,state).

    from read_trace import read_trace
    t = read_trace(path)   # t.frames, t.rows, t.by_frame[f]

CLI: python tools/read_trace.py <trace.csv>
"""

from __future__ import annotations

import csv
import sys
from dataclasses import dataclass
from pathlib import Path

COLUMNS = ["f", "id", "parent", "sent", "recv", "B", "E", "charge", "state"]


@dataclass(frozen=True)
class Row:
    f: int
    id: int
    parent: int
    sent: int
    recv: int
    B: int
    E: float
    charge: float
    state: str


@dataclass
class Trace:
    rows: list[Row]
    frames: list[int]
    by_frame: dict[int, list[Row]]


def read_trace(path: Path | str) -> Trace:
    path = Path(path)
    with open(path, "r", encoding="utf-8", newline="") as fh:
        reader = csv.reader(fh)
        header = next(reader, None)
        if header != COLUMNS:
            sys.exit(f"{path}: unexpected header {header}; expected {COLUMNS}")
        rows: list[Row] = []
        for line_no, rec in enumerate(reader, start=2):
            if len(rec) != len(COLUMNS):
                sys.exit(f"{path}:{line_no}: {len(rec)} fields, expected {len(COLUMNS)}")
            rows.append(Row(int(rec[0]), int(rec[1]), int(rec[2]), int(rec[3]), int(rec[4]),
                            int(rec[5]), float(rec[6]), float(rec[7]), rec[8]))
    by_frame: dict[int, list[Row]] = {}
    for r in rows:
        by_frame.setdefault(r.f, []).append(r)
    for f in by_frame:
        by_frame[f].sort(key=lambda r: r.id)
    return Trace(rows=rows, frames=sorted(by_frame), by_frame=by_frame)


def main(argv: list[str]) -> None:
    if len(argv) != 2 or argv[1] in ("-h", "--help"):
        sys.exit("usage: read_trace.py <trace.csv>\n\n"
                 "Summarise a trace file: row count, frame range and rows per frame.\n"
                 "Traces are written only when tracing is enabled in the config.")
    t = read_trace(argv[1])
    per_frame = {len(v) for v in t.by_frame.values()}
    print(f"{argv[1]}: {len(t.rows)} rows, {len(t.frames)} frames "
          f"({t.frames[0]}..{t.frames[-1]}), rows per frame {sorted(per_frame)}"
          if t.frames else f"{argv[1]}: 0 rows")


if __name__ == "__main__":
    main(sys.argv)
