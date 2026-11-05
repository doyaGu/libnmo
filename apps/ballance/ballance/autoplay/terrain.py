"""The ground a ball rolls on, sampled on a grid of CELL.

Each cell holds the heights the ball may roll at there (its layers): the
tops of the floors (Phys_Floors, faces flatter than FLOOR_SLOPE) and the
strips between a rail's two tubes.  A rail's tubes are found by their rings,
paired across GAUGE, and the strip between them runs only as far as they
stay close enough to hold the ball; where they part, it falls through.

Layers the ball can't hold are dropped: within its radius of a wall (a rise
past STEEP), and, off the rails, on less floor than it is wide (a rim's
top).  The terrain also keeps the direction each rail runs in its cells, the
cells around the boxes the ball would push, and where the markers and
elements are.
"""

from __future__ import annotations

import math
from collections import defaultdict
from dataclasses import dataclass

from ..game.catalog import MARKERS, element_kind
from ..kit.rails import GAUGE
from ..view.scene import SceneObject

CELL = 1.0
MERGE = 0.6  # heights closer than this in a cell are one floor
CLIMB = 0.65  # most a cell may rise from the one beside it (a slope; more is a step)
STEEP = 3.0  # a step the ball can still take with a run at it; higher is a wall
OVERHEAD = 8.0  # higher than this above it, a floor is no wall to it
RADIUS = 1.9  # how far the ball keeps from a wall (its radius, about)
FLOOR_SLOPE = 0.6  # the least upward part of a floor face's normal
RING_EDGE = 0.4  # a ring's edges are shorter (an octagon's of radius 0.35 is 0.27)
LEVEL_RINGS = 1.0  # paired rings are level with each other within this
PARTING = 4.8  # tubes further apart than this, the ball falls between them
BOX_CLEAR = 2.5  # around a box, the cells the ball would push it in
# the cells under the ball: those within two cells of it
SUPPORT = [(di, dj) for di in range(-2, 3) for dj in range(-2, 3) if di * di + dj * dj <= 4]

Cell = tuple[int, int]
Point3 = tuple[float, float, float]
Floor = tuple[float, float, float, float, float]  # x0, x1, z0, z1, height


@dataclass
class Terrain:
    layers: dict[Cell, list[float]]  # the heights in a cell, lowest first
    rails: set[Cell]  # the cells a rail's strip covers
    tangents: dict[Cell, tuple[float, float]]  # the way a rail runs there, on XZ
    boxes: set[Cell]  # the cells a box would be pushed through
    marks: dict[str, Point3]  # markers and elements, where they are

    def without(self, x: float, z: float, reach: float) -> None:
        """Takes out the cells within reach of (x, z)."""
        for cell in [
            c for c in self.layers if math.hypot(centre(c[0]) - x, centre(c[1]) - z) < reach
        ]:
            del self.layers[cell]


def centre(index: int) -> float:
    """The middle of a cell along one axis."""
    return (index + 0.5) * CELL


def terrain(scene: list[SceneObject], floors: list[Floor] = ()) -> Terrain:
    """The scene's ground, with floors added (as a level has them once a
    mechanism has moved): each solid, with nothing under it."""
    floor_triangles, rail_triangles, rail_ways = [], [], []
    for x0, x1, z0, z1, h in floors:
        floor_triangles += [
            ((x0, h, z0), (x1, h, z0), (x1, h, z1)),
            ((x0, h, z0), (x1, h, z1), (x0, h, z1)),
        ]
    marks, boxes = {}, set()
    for obj in scene:
        if 'P_Box' in obj.groups and obj.triangles:
            boxes |= _around(obj, BOX_CLEAR)
        if obj.name.startswith(tuple(MARKERS)) or element_kind(obj.name):
            marks[obj.name] = obj.at
        if 'Phys_Floors' in obj.groups and not obj.name.startswith('Tower'):
            floor_triangles += [t.points for t in obj.triangles if _flat(t.points)]
        if 'Phys_FloorRails' in obj.groups:
            triangles, ways = _rail_strips(obj)
            rail_triangles += triangles
            rail_ways += ways
    on_floors: dict[Cell, list[float]] = defaultdict(list)
    _rasterize(floor_triangles, on_floors)
    on_rails: dict[Cell, list[float]] = defaultdict(list)
    tangents: dict[Cell, tuple[float, float]] = {}
    _rasterize(rail_triangles, on_rails, rail_ways, tangents)
    layers = {}
    for cell in set(on_floors) | set(on_rails):
        merged: list[float] = []
        for h in sorted(on_floors.get(cell, []) + on_rails.get(cell, [])):
            if merged and h - merged[-1] < MERGE:
                merged[-1] = max(merged[-1], h)
            else:
                merged.append(h)
        for x0, x1, z0, z1, h in floors:
            if x0 <= centre(cell[0]) <= x1 and z0 <= centre(cell[1]) <= z1:
                merged = [m for m in merged if m > h - MERGE]
        layers[cell] = merged
    rails = set(on_rails)
    layers = _off_walls(layers)
    layers = _held(layers, rails)
    return Terrain(layers, rails, tangents, boxes, marks)


def _flat(p: list[Point3]) -> bool:
    n = (
        (p[1][1] - p[0][1]) * (p[2][2] - p[0][2]) - (p[1][2] - p[0][2]) * (p[2][1] - p[0][1]),
        (p[1][2] - p[0][2]) * (p[2][0] - p[0][0]) - (p[1][0] - p[0][0]) * (p[2][2] - p[0][2]),
        (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]),
    )
    length = math.sqrt(sum(v * v for v in n))
    return length > 1e-9 and abs(n[1]) / length > FLOOR_SLOPE


def _around(obj: SceneObject, clear: float) -> set[Cell]:
    """The cells of an object's footprint, clear more each way."""
    ps = [p for t in obj.triangles for p in t.points]
    x0, x1 = min(p[0] for p in ps) - clear, max(p[0] for p in ps) + clear
    z0, z1 = min(p[2] for p in ps) - clear, max(p[2] for p in ps) + clear
    return {
        (i, j)
        for i in range(math.floor(x0 / CELL), math.ceil(x1 / CELL))
        for j in range(math.floor(z0 / CELL), math.ceil(z1 / CELL))
    }


def _rasterize(
    triangles: list, cells: dict, ways: list | None = None, tangents: dict | None = None
) -> None:
    """Each triangle's height at the middles of the cells it covers (and,
    with ways, its way into tangents)."""
    for n, ((ax, ay, az), (bx, by, bz), (cx, cy, cz)) in enumerate(triangles):
        d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(d) < 1e-9:
            continue
        for i in range(math.floor(min(ax, bx, cx) / CELL), math.ceil(max(ax, bx, cx) / CELL) + 1):
            for j in range(
                math.floor(min(az, bz, cz) / CELL), math.ceil(max(az, bz, cz) / CELL) + 1
            ):
                x, z = centre(i), centre(j)
                l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d
                l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d
                l3 = 1 - l1 - l2
                if min(l1, l2, l3) >= -1e-6:
                    cells[(i, j)].append(l1 * ay + l2 * by + l3 * cy)
                    if ways is not None:
                        tangents[(i, j)] = ways[n]


def _rings(obj: SceneObject) -> tuple[dict, dict]:
    """A rail's rings, by their middles, and the rings each one's tube
    joins it to: a ring is a loop of edges shorter than RING_EDGE."""

    def key(p):
        return round(p[0], 3), round(p[1], 3), round(p[2], 3)

    parent: dict = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            x = parent[x]
        return x

    for t in obj.triangles:
        for k in range(3):
            a, b = key(t.points[k]), key(t.points[(k + 1) % 3])
            find(a)
            find(b)
            if math.dist(a, b) < RING_EDGE:
                parent[find(a)] = find(b)
    loops = defaultdict(list)
    for x in list(parent):
        loops[find(x)].append(x)
    ring_of, middle = {}, {}
    for r, vs in loops.items():
        if len(vs) < 5:
            continue
        a, b = max(((a, b) for a in vs for b in vs), key=lambda pair: math.dist(*pair))
        middle[r] = tuple((a[k] + b[k]) / 2 for k in range(3))
        for x in vs:
            ring_of[x] = r
    joined = defaultdict(set)
    for t in obj.triangles:
        rs = {ring_of.get(key(p)) for p in t.points} - {None}
        if len(rs) == 2:
            a, b = rs
            joined[a].add(b)
            joined[b].add(a)
    return middle, joined


def _rail_strips(obj: SceneObject) -> tuple[list, list]:
    """The quads between a rail's two tubes, as triangles, and the way the
    rail runs along each: each ring paired with the one on the other tube
    nearest it, level with it and GAUGE (or, as the tubes part, up to
    PARTING) away, when that one's nearest is it too; two pairs whose rings
    the tubes join make a quad."""
    middle, joined = _rings(obj)
    tube = {}
    for r in middle:
        if r in tube:
            continue
        tube[r] = r
        stack = [r]
        while stack:
            for q in joined[stack.pop()]:
                if q not in tube:
                    tube[q] = r
                    stack.append(q)
    nearest = {}
    for r, c in middle.items():
        best = None
        for q, d in middle.items():
            if tube[q] == tube[r] or abs(d[1] - c[1]) > LEVEL_RINGS:
                continue
            dist = math.dist(c, d)
            if GAUGE - 0.4 < dist < PARTING and (best is None or dist < best[0]):
                best = (dist, q)
        if best:
            nearest[r] = best[1]
    partner = {r: q for r, q in nearest.items() if nearest.get(q) == r}
    triangles, ways = [], []
    for a, b in partner.items():
        for a2 in joined[a]:
            b2 = partner.get(a2)
            if b2 is not None and b2 in joined[b] and a < a2:
                pa, pb, pa2, pb2 = middle[a], middle[b], middle[a2], middle[b2]
                triangles += [(pa, pa2, pb2), (pa, pb2, pb)]
                dx, dz = pa2[0] - pa[0], pa2[2] - pa[2]
                length = math.hypot(dx, dz) or 1.0
                ways += [(dx / length, dz / length)] * 2
    return triangles, ways


def _off_walls(layers: dict[Cell, list[float]]) -> dict[Cell, list[float]]:
    """The layers RADIUS or more from a wall: a cell near, rising past STEEP
    (but not OVERHEAD), with nothing at the layer's height."""
    r = int(math.ceil(RADIUS / CELL))
    near = [
        (di, dj)
        for di in range(-r, r + 1)
        for dj in range(-r, r + 1)
        if (di or dj) and math.hypot(di, dj) * CELL <= RADIUS
    ]
    kept = {}
    for (i, j), hs in layers.items():
        keep = []
        for h in hs:
            walled = False
            for di, dj in near:
                there = layers.get((i + di, j + dj), [])
                if any(STEEP < hh - h < OVERHEAD for hh in there) and not any(
                    abs(hh - h) <= CLIMB for hh in there
                ):
                    walled = True
            if not walled:
                keep.append(h)
        if keep:
            kept[(i, j)] = keep
    return kept


def _held(layers: dict[Cell, list[float]], rails: set[Cell]) -> dict[Cell, list[float]]:
    """The layers with floor at their height under nearly all the ball
    (SUPPORT, three cells short at most), and those on a rail."""
    kept = {}
    for (i, j), hs in layers.items():
        keep = []
        for h in hs:
            under = sum(
                1
                for di, dj in SUPPORT
                if any(abs(hh - h) <= CLIMB for hh in layers.get((i + di, j + dj), []))
            )
            if under >= len(SUPPORT) - 3 or (i, j) in rails:
                keep.append(h)
        if keep:
            kept[(i, j)] = keep
    return kept
