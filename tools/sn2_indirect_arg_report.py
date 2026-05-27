#!/usr/bin/env python3
"""Summarize SN2 ExecuteIndirect argument-buffer readback rows.

Input is UEVR log.txt lines emitted by:
  [SN2-Readback-Done] tag=EIARG-<eye>-<kind>-<crc>-<seq> ...
  [SN2-DispatchMesh-Args] eye=<eye> ... ms_crc=... ps_crc=... groups=(x,y,z)

The useful distinction for the SN2 right-eye bug is:
  - left has nonzero work and right has no matching work event
  - right has a matching work event but its first arg/count is zero

This parser groups by command-signature kind + shader CRC and reports both.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import pathlib
import re
from typing import Iterable


ROW_RE = re.compile(
    r"tag=EIARG-([LRU])-([A-Z_]+)-([0-9a-fA-F]{8})-(\d+).*?"
    r"words=\[([0-9a-fA-F]{8}) ([0-9a-fA-F]{8}) ([0-9a-fA-F]{8})"
)

DIRECT_MESH_RE = re.compile(
    r"\[SN2-DispatchMesh-Args\] eye=([LRU]).*?"
    r"ms_crc=0x([0-9a-fA-F]{8}).*?"
    r"ps_crc=0x([0-9a-fA-F]{8}).*?"
    r"groups=\((\d+),(\d+),(\d+)\).*?seq=(\d+)"
)


@dataclasses.dataclass
class Row:
    eye: str
    kind: str
    crc: str
    seq: int
    x: int
    y: int
    z: int
    line: str


@dataclasses.dataclass
class DirectMeshRow:
    eye: str
    ms_crc: str
    ps_crc: str
    seq: int
    x: int
    y: int
    z: int
    line: str


def parse_rows(path: pathlib.Path) -> Iterable[Row]:
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = ROW_RE.search(line)
            if not m:
                continue
            eye, kind, crc, seq_s, x_s, y_s, z_s = m.groups()
            yield Row(
                eye=eye,
                kind=kind,
                crc=crc.lower(),
                seq=int(seq_s),
                x=int(x_s, 16),
                y=int(y_s, 16),
                z=int(z_s, 16),
                line=line.strip(),
            )


def parse_direct_mesh_rows(path: pathlib.Path) -> Iterable[DirectMeshRow]:
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = DIRECT_MESH_RE.search(line)
            if not m:
                continue
            eye, ms_crc, ps_crc, x_s, y_s, z_s, seq_s = m.groups()
            yield DirectMeshRow(
                eye=eye,
                ms_crc=ms_crc.lower(),
                ps_crc=ps_crc.lower(),
                seq=int(seq_s),
                x=int(x_s),
                y=int(y_s),
                z=int(z_s),
                line=line.strip(),
            )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=pathlib.Path)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--examples", type=int, default=12)
    args = ap.parse_args()

    rows = list(parse_rows(args.log))
    print(f"rows={len(rows)}")
    direct_mesh_rows = list(parse_direct_mesh_rows(args.log))
    print(f"direct_dispatch_mesh_rows={len(direct_mesh_rows)}")
    if not rows and not direct_mesh_rows:
        return 0
    if rows:
        print(f"seq_range={rows[0].seq}..{rows[-1].seq}")

    by_group: dict[tuple[str, str], list[Row]] = collections.defaultdict(list)
    for row in rows:
        by_group[(row.kind, row.crc)].append(row)

    summaries = []
    for (kind, crc), group_rows in by_group.items():
        counts = collections.Counter(r.eye for r in group_rows)
        zero = collections.Counter(r.eye for r in group_rows if r.x == 0)
        nonzero = collections.Counter(r.eye for r in group_rows if r.x != 0)
        max_x = {eye: max((r.x for r in group_rows if r.eye == eye), default=0) for eye in "LRU"}
        score = 0
        if nonzero["L"] and counts["R"] == 0:
            score += 1000 + nonzero["L"]
        if nonzero["L"] and zero["R"] and nonzero["R"] == 0:
            score += 800 + zero["R"]
        if counts["L"] and counts["R"] == 0:
            score += 100 + counts["L"]
        summaries.append((score, kind, crc, counts, zero, nonzero, max_x))

    summaries.sort(key=lambda x: (x[0], x[3]["L"] + x[3]["R"] + x[3]["U"]), reverse=True)

    print("\nTop grouped readbacks:")
    print("score kind crc counts(L/R/U) zero(L/R/U) nonzero(L/R/U) max_x(L/R/U)")
    for score, kind, crc, counts, zero, nonzero, max_x in summaries[: args.top]:
        print(
            f"{score:5d} {kind:13s} {crc} "
            f"{counts['L']:3d}/{counts['R']:3d}/{counts['U']:3d} "
            f"{zero['L']:3d}/{zero['R']:3d}/{zero['U']:3d} "
            f"{nonzero['L']:3d}/{nonzero['R']:3d}/{nonzero['U']:3d} "
            f"{max_x['L']:6d}/{max_x['R']:6d}/{max_x['U']:6d}"
        )

    print("\nSuspect examples:")
    shown = 0
    for score, kind, crc, counts, zero, nonzero, _ in summaries:
        if score <= 0:
            continue
        group_rows = by_group[(kind, crc)]
        why = []
        if nonzero["L"] and counts["R"] == 0:
            why.append("left_nonzero_right_absent")
        if nonzero["L"] and zero["R"] and nonzero["R"] == 0:
            why.append("left_nonzero_right_zero")
        if counts["L"] and counts["R"] == 0 and not why:
            why.append("left_present_right_absent")
        print(f"- {kind} crc={crc} score={score} reason={','.join(why)}")
        for row in group_rows[:3]:
            print(f"  {row.line[:260]}")
        shown += 1
        if shown >= args.examples:
            break

    if direct_mesh_rows:
        by_mesh: dict[tuple[str, str], list[DirectMeshRow]] = collections.defaultdict(list)
        for row in direct_mesh_rows:
            key_crc = row.ps_crc if row.ps_crc != "00000000" else row.ms_crc
            by_mesh[(row.ms_crc, key_crc)].append(row)

        mesh_summaries = []
        for (ms_crc, key_crc), group_rows in by_mesh.items():
            counts = collections.Counter(r.eye for r in group_rows)
            zero = collections.Counter(r.eye for r in group_rows if r.x == 0)
            nonzero = collections.Counter(r.eye for r in group_rows if r.x != 0)
            max_x = {eye: max((r.x for r in group_rows if r.eye == eye), default=0) for eye in "LRU"}
            score = 0
            if nonzero["L"] and counts["R"] == 0:
                score += 1000 + nonzero["L"]
            if nonzero["L"] and zero["R"] and nonzero["R"] == 0:
                score += 800 + zero["R"]
            mesh_summaries.append((score, ms_crc, key_crc, counts, zero, nonzero, max_x))

        mesh_summaries.sort(key=lambda x: (x[0], x[3]["L"] + x[3]["R"] + x[3]["U"]), reverse=True)

        print("\nDirect DispatchMesh calls:")
        print("score ms_crc key_crc counts(L/R/U) zero(L/R/U) nonzero(L/R/U) max_x(L/R/U)")
        for score, ms_crc, key_crc, counts, zero, nonzero, max_x in mesh_summaries[: args.top]:
            print(
                f"{score:5d} {ms_crc} {key_crc} "
                f"{counts['L']:3d}/{counts['R']:3d}/{counts['U']:3d} "
                f"{zero['L']:3d}/{zero['R']:3d}/{zero['U']:3d} "
                f"{nonzero['L']:3d}/{nonzero['R']:3d}/{nonzero['U']:3d} "
                f"{max_x['L']:6d}/{max_x['R']:6d}/{max_x['U']:6d}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
