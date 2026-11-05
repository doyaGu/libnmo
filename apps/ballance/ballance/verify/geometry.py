"""Faults in a level's floors, found from its triangles in the world.

- open wall ends: a floor wall whose upright edge no other wall meets, so
  the floor's end or side is left open there;
- holes: small gaps in the floor tops, floor all round at about one height
  (gaps open to the air: one over another floor is not found);
- overlaps: floor tops of two objects lying on one another (they flicker).

Only the floors count (Phys_Floors), hidden ones too; lantern towers and
decorative column tops are left out, being shaped otherwise.
"""

from __future__ import annotations

import math
import re
from collections import defaultdict
from dataclasses import dataclass

from ..view.scene import SceneObject

UPRIGHT = 0.3  # a face is a wall when its normal leans no more up than this
EDGE_RISE = 1.0  # an upright edge rises at least this
SAME_SPOT = 0.05  # and its ends are this close across
COVERED = 0.8  # other walls covering this much of an edge meet it
CELL = 0.25  # floor tops are sampled at cells this size
HOLE_REACH = 12  # cells: floor within this of a hole on all four sides
HOLE_LEVEL = 1.5  # at heights this close to one another
OVERLAP = 0.3  # tops of two objects closer than this lie on one another

Point3 = tuple[float, float, float]


@dataclass
class Finding:
    kind: str  # "open wall", "hole" or "overlap"
    where: Point3
    what: str

    def __str__(self) -> str:
        x, y, z = self.where
        return f'{self.kind} at ({x:.1f}, {y:.1f}, {z:.1f}): {self.what}'


def _floors(scene: list[SceneObject]) -> list[SceneObject]:
    return [
        obj
        for obj in scene
        if 'Phys_Floors' in obj.groups
        and not re.search(r'(?:^|_)(?:Tower|Floor_Columns_Top)(?:_?\d+)?$', obj.name)
    ]


def _up(t) -> float:
    """How far a triangle's face leans up, from -1 (facing down) through 0
    (a wall) to 1: its own normal, facing as its corners' normals do where
    it has them (they are smoothed), else as it is wound."""
    a, b, c = t.points
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    if t.normals and sum(n[i] * sum(m[i] for m in t.normals) for i in range(3)) < 0:
        n = [-q for q in n]
    return n[1] / (math.sqrt(sum(q * q for q in n)) or 1.0)


def _face_interval(lo: Point3, hi: Point3, t) -> tuple[float, float] | None:
    """Part of an edge whose projection falls on a nearby wall triangle."""
    a, b, c = t.points
    u = tuple(b[i] - a[i] for i in range(3))
    v = tuple(c[i] - a[i] for i in range(3))
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])

    def dot(x, y):
        return sum(x[i] * y[i] for i in range(3))

    uu, uv, vv = dot(u, u), dot(u, v), dot(v, v)
    den = uu * vv - uv * uv
    if den <= 1e-12:
        return None
    distance = SAME_SPOT * math.sqrt(dot(n, n))

    def constraints(p):
        w = tuple(p[i] - a[i] for i in range(3))
        wu, wv = dot(w, u), dot(w, v)
        s, r = (wu * vv - wv * uv) / den, (wv * uu - wu * uv) / den
        plane = dot(w, n)
        return (s + 1e-7, r + 1e-7, 1 - s - r + 1e-7, distance - plane, distance + plane)

    start, end = 0.0, 1.0
    for x, y in zip(constraints(lo), constraints(hi)):
        if x < 0 and y < 0:
            return None
        if x < 0:
            start = max(start, -x / (y - x))
        elif y < 0:
            end = min(end, x / (x - y))
        if start >= end:
            return None
    return start, end


def _covered_length(intervals: list[tuple[float, float]]) -> float:
    """Length of the union, counting overlapping spans only once."""
    if not intervals:
        return 0.0
    ordered = sorted(intervals)
    covered = 0.0
    start, end = ordered[0]
    for a, b in ordered[1:]:
        if a > end:
            covered += end - start
            start, end = a, b
        else:
            end = max(end, b)
    return covered + end - start


def _face_covered(lo: Point3, hi: Point3, source, walls: list) -> bool:
    """Whether wall faces cover most of an edge, including T-junctions."""
    intervals = []
    for t, bounds in walls:
        if t is source or any(
            max(lo[i], hi[i]) + SAME_SPOT < bounds[i][0]
            or min(lo[i], hi[i]) - SAME_SPOT > bounds[i][1]
            for i in range(3)
        ):
            continue
        interval = _face_interval(lo, hi, t)
        if interval:
            intervals.append(interval)
    return _covered_length(intervals) > COVERED


def open_walls(scene: list[SceneObject]) -> list[Finding]:
    """Upright wall edges that no other wall meets (or stands on or under)."""
    q = SAME_SPOT
    columns = defaultdict(list)
    walls = []
    for obj in _floors(scene):
        for t in obj.triangles:
            if abs(_up(t)) > UPRIGHT:
                continue
            bounds = [(min(p[i] for p in t.points), max(p[i] for p in t.points)) for i in range(3)]
            walls.append((t, bounds))
            for p, r in zip(t.points, t.points[1:] + t.points[:1]):
                if abs(p[1] - r[1]) > EDGE_RISE and math.hypot(p[0] - r[0], p[2] - r[2]) < q:
                    lo, hi = sorted((p, r), key=lambda s: s[1])
                    columns[(round(lo[0] / q), round(lo[2] / q))].append((lo, hi, obj.name, t))
    found = []
    for (i, j), spans in columns.items():
        for k, (lo, hi, name, source) in enumerate(spans):
            y0, y1 = lo[1], hi[1]
            others = [(a[1], b[1]) for n, (a, b, _, _) in enumerate(spans) if n != k]
            cover = _covered_length(
                [(max(y0, s0), min(y1, s1)) for s0, s1 in others if min(y1, s1) > max(y0, s0)]
            )
            stacked = any(abs(s1 - y0) < q or abs(s0 - y1) < q for s0, s1 in others)
            if (
                cover <= COVERED * (y1 - y0)
                and not stacked
                and not _face_covered(lo, hi, source, walls)
            ):
                found.append(Finding('open wall', (i * q, y1, j * q), name))
    return found


def _tops(scene: list[SceneObject]) -> dict[tuple[int, int], list[tuple[float, str]]]:
    """The floor tops (faces leaning up) over each cell's middle: (height,
    object)."""
    tops: dict[tuple[int, int], list[tuple[float, str]]] = defaultdict(list)
    for obj in _floors(scene):
        for t in obj.triangles:
            if _up(t) < UPRIGHT:
                continue
            a, b, c = t.points
            den = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(den) < 1e-9:
                continue
            for i in range(
                math.floor(min(a[0], b[0], c[0]) / CELL),
                math.floor(max(a[0], b[0], c[0]) / CELL) + 1,
            ):
                for j in range(
                    math.floor(min(a[2], b[2], c[2]) / CELL),
                    math.floor(max(a[2], b[2], c[2]) / CELL) + 1,
                ):
                    x, z = (i + 0.5) * CELL, (j + 0.5) * CELL
                    w1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / den
                    w2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / den
                    w3 = 1.0 - w1 - w2
                    if min(w1, w2, w3) >= -1e-6:
                        tops[(i, j)].append((w1 * a[1] + w2 * b[1] + w3 * c[1], obj.name))
    return tops


def holes(scene: list[SceneObject], tops=None) -> list[Finding]:
    """Gaps in the floor tops with floor at about one height on all four
    sides within HOLE_REACH, each run of them once."""
    tops = tops if tops is not None else _tops(scene)

    def bounded(lines: dict[int, list[int]]) -> dict[tuple[int, int], tuple[int, int]]:
        """The empty cells along each line with floor within HOLE_REACH both
        ways, and those floor cells' places along it."""
        found = {}
        for at, row in lines.items():
            row.sort()
            for a, b in zip(row, row[1:]):
                for k in range(max(a + 1, b - HOLE_REACH), min(b, a + HOLE_REACH + 1)):
                    found[(at, k)] = (a, b)
        return found

    rows: dict[int, list[int]] = defaultdict(list)
    columns: dict[int, list[int]] = defaultdict(list)
    for i, j in tops:
        rows[j].append(i)
        columns[i].append(j)
    across = bounded(rows)  # (j, i): the floor's i either side
    along = bounded(columns)  # (i, j): the floor's j either side
    gaps: dict[tuple[int, int], float] = {}
    for (j, i), (i0, i1) in across.items():
        if (i, j) not in along:
            continue
        j0, j1 = along[(i, j)]
        sides = [[y for y, _ in tops[cell]] for cell in ((i0, j), (i1, j), (i, j0), (i, j1))]
        level = next(
            (
                y
                for y in sides[0]
                if all(any(abs(y - yy) < HOLE_LEVEL for yy in side) for side in sides[1:])
            ),
            None,
        )
        if level is not None:
            gaps[(i, j)] = level
    # each run of them once
    found, seen = [], set()
    for cell in gaps:
        if cell in seen:
            continue
        stack, run = [cell], []
        seen.add(cell)
        while stack:
            i, j = stack.pop()
            run.append((i, j))
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                near = (i + di, j + dj)
                if near in gaps and near not in seen:
                    seen.add(near)
                    stack.append(near)
        owners = sorted(
            {
                name
                for i, j in run
                for di in range(-HOLE_REACH, HOLE_REACH + 1)
                for near in ((i + di, j), (i, j + di))
                for _, name in tops.get(near, [])
            }
        )
        i, j = run[0]
        found.append(
            Finding(
                'hole',
                ((i + 0.5) * CELL, gaps[run[0]], (j + 0.5) * CELL),
                f'{len(run)} cells, in {", ".join(owners)}',
            )
        )
    return found


def overlaps(scene: list[SceneObject], tops=None) -> list[Finding]:
    """Floor tops of two objects within OVERLAP of each other, once for each
    pair of objects."""
    tops = tops if tops is not None else _tops(scene)
    pairs: dict[tuple[str, str], list] = defaultdict(list)
    for (i, j), here in tops.items():
        for k, (y, name) in enumerate(here):
            for y2, name2 in here[k + 1 :]:
                if name != name2 and abs(y - y2) < OVERLAP:
                    pairs[tuple(sorted((name, name2)))].append(
                        ((i + 0.5) * CELL, y, (j + 0.5) * CELL)
                    )
    return [
        Finding('overlap', cells[0], f'{len(cells)} cells of {a} and {b}')
        for (a, b), cells in sorted(pairs.items())
    ]


def check(scene: list[SceneObject]) -> list[Finding]:
    """Every fault found."""
    tops = _tops(scene)
    return open_walls(scene) + holes(scene, tops) + overlaps(scene, tops)
