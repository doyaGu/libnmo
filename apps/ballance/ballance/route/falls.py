"""The fall zones under a level: hidden boxes that make a ball touching one
fall off ("Ball Off"), each its own object, as the originals have a few
under their sections."""

from __future__ import annotations

import math
from pathlib import Path

from ..core.obj import Box, write_box
from ..game.measures import FALL_BOTTOM, FALL_MARGIN, FALL_STEP, FALL_TILE, FALL_TOP

Point3 = tuple[float, float, float]


def floor_points(objects: list[dict], out: Path) -> list[Point3]:
    """The corners of the manifest's floors and rails, placed in the world."""
    points = []
    for obj in objects:
        if not any(group.startswith("Phys_") for group in obj["groups"]):
            continue
        px, py, pz = obj["transform"]["position"]
        t = math.radians(obj["transform"].get("rotation_euler_deg", [0, 0, 0])[1])
        c, s = math.cos(t), math.sin(t)
        for line in (out / obj["mesh"]["obj"]).read_text(encoding="utf-8").splitlines():
            if line.startswith("v "):
                x, y, z = (float(v) for v in line.split()[1:4])
                points.append((px + x * c + z * s, py + y, pz - x * s + z * c))
    return points


def fall_boxes(points: list[Point3]) -> list[Box]:
    """Boxes FALL_TOP to FALL_BOTTOM below the lowest floor of each FALL_TILE
    square that has floors, and of the squares round those (as deep as the
    deepest of their neighbours), the squares at one depth joined; and one
    under all of them, FALL_MARGIN beyond, for a ball thrown further."""
    lows: dict[tuple[int, int], float] = {}
    for x, y, z in points:
        tile = (math.floor(x / FALL_TILE), math.floor(z / FALL_TILE))
        lows[tile] = min(lows.get(tile, math.inf), y)
    tops = {tile: math.floor((low - FALL_TOP) / FALL_STEP) * FALL_STEP
            for tile, low in lows.items()}
    for (i, j), top in list(tops.items()):
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                near = (i + di, j + dj)
                if near not in lows:
                    tops[near] = min(tops.get(near, math.inf), top)
    # runs along X of squares at one depth, then runs of those along Z
    runs: dict[tuple[int, int, float], list[int]] = {}
    for j in sorted({j for _, j in tops}):
        row = sorted(i for i, jj in tops if jj == j)
        start = None
        for k, i in enumerate(row):
            if start is None:
                start = i
            ends = (k + 1 == len(row) or row[k + 1] != i + 1 or
                    tops[(row[k + 1], j)] != tops[(i, j)])
            if ends:
                runs.setdefault((start, i, tops[(i, j)]), []).append(j)
                start = None
    depth = FALL_BOTTOM - FALL_TOP
    boxes = []
    for (i0, i1, top), rows in sorted(runs.items()):
        rows.sort()
        first = rows[0]
        for k, j in enumerate(rows):
            if k + 1 == len(rows) or rows[k + 1] != j + 1:
                boxes.append((i0 * FALL_TILE, (i1 + 1) * FALL_TILE, top - depth, top,
                              first * FALL_TILE, (j + 1) * FALL_TILE))
                if k + 1 < len(rows):
                    first = rows[k + 1]
    x0 = min(b[0] for b in boxes) - FALL_MARGIN
    x1 = max(b[1] for b in boxes) + FALL_MARGIN
    z0 = min(b[4] for b in boxes) - FALL_MARGIN
    z1 = max(b[5] for b in boxes) + FALL_MARGIN
    low = min(b[2] for b in boxes)
    boxes.append((x0, x1, low - depth, low, z0, z1))
    return boxes


def write_fall_zones(objects: list[dict], out: Path) -> list[str]:
    """Writes the fall boxes under the manifest's floors as depth<N>.obj;
    returns the meshes' names."""
    meshes = []
    for index, box in enumerate(fall_boxes(floor_points(objects, out)), start=1):
        meshes.append(f"depth{index}.obj")
        write_box(out / meshes[-1], box)
    return meshes
