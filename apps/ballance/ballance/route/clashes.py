"""Parts of a laid-out level in one another's way.

The builder keeps one grid's floors apart; everything else meets here.  Each
piece of the route (ballance.route.layout.Part) takes up boxes: its floors,
pieces off the grid, cut floors, rails, elements, pads and balloon, as they
stand.  The room the ball rolls through is CLASH_HEADROOM above the floors
a piece lays and CLASH_RAIL_ROOM above its rails' middles, and the room its
elements move in is what they take up.  A piece's boxes in that room over
another piece's floors are in the ball's way, unless one goes on from the
other (or a branch comes back to it), or only rails and falls come between
them (a rail's end over the area it drops into, a log reaching back under
the rail it is dropped onto from)."""

from __future__ import annotations

import math
from pathlib import Path

from ..core.obj import Box, Mesh
from ..game.measures import CLASH_HEADROOM, CLASH_OVERLAP, CLASH_RAIL_ROOM, CLASH_RAIL_STEP
from ..kit.floors import CELL, RISE, WALL
from ..kit.railings import RADIUS, centre_line
from ..kit.rails import GAUGE, TUBE
from .assets import Assets
from .layout import Layout, Part
from .modules import MODULE_FLOORS

# Pieces the ball flies along, not rolling on a floor.
FLOWN = ("rail", "rail_turn", "fall")
# Elements the ball passes through or that are not in its way, and the
# sacks, which swing into it on purpose (over a road, or under a beam's crest
# into the beam crossing below).
PASSED = ("P_Extra_Point", "P_Extra_Life", "P_Trafo_", "P_Modul_26")


def _bounds(points: list[tuple[float, float, float]]) -> Box | None:
    if not points:
        return None
    return (min(p[0] for p in points), max(p[0] for p in points),
            min(p[1] for p in points), max(p[1] for p in points),
            min(p[2] for p in points), max(p[2] for p in points))


def _placed(path: Path, position: list[float], turn: float) -> Box | None:
    """The bounds of an OBJ placed and turned as the manifest places it."""
    px, py, pz = position
    t = math.radians(turn)
    c, s = math.cos(t), math.sin(t)
    points = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("v "):
            x, y, z = (float(v) for v in line.split()[1:4])
            points.append((px + x * c + z * s, py + y, pz - x * s + z * c))
    return _bounds(points)


def _room(box: Box, top: float | None = None) -> Box:
    """The room above a floor whose top is at top (the box's, unless given)."""
    x0, x1, y0, y1, z0, z1 = box
    return (x0, x1, y0, (y1 if top is None else top) + CLASH_HEADROOM, z0, z1)


def _boxes(part: Part, assets: Assets) -> tuple[list[Box], list[Box]]:
    """What a part takes up, and the room the ball rolls through on it."""
    solid: list[Box] = []
    room: list[Box] = []
    for grid, piece in part.floors:
        # a cell's width at a time along a sloping road
        i0, j0, i1, j1 = piece.rect
        if piece.y0 == piece.y1:
            strips = [(i0, j0, i1, j1)]
        elif piece.axis == 0:
            strips = [(i, j0, i + 1, j1) for i in range(i0, i1)]
        else:
            strips = [(i0, j, i1, j + 1) for j in range(j0, j1)]
        for a0, b0, a1, b1 in strips:
            heights = [piece.height(i, j) for i in (a0, a1) for j in (b0, b1)]
            low, high = min(heights), max(heights)
            box = (grid.ox + a0 * CELL, grid.ox + a1 * CELL, high + RISE - WALL, high + RISE,
                   grid.oz + b0 * CELL, grid.oz + b1 * CELL)
            solid.append(box)
            room.append(_room((box[0], box[1], low, high + RISE, box[4], box[5])))
    for function, (x, z), *rest in part.extras:
        mesh = Mesh()
        function(mesh, x, z, *rest)
        box = _bounds([tuple(float(v) for v in line.split()[1:4])
                       for line in mesh.lines if line.startswith("v ")])
        if box:
            solid.append(box)
            room.append(_room(box))
    for kind, _, position, turn in part.module_floors:
        for floor in MODULE_FLOORS[kind]:
            box = _placed(assets.out / assets.mesh(floor.name), position, turn)
            if box:
                solid.append(box)
                room.append(_room(box))
    for kind, _, position, *turn in part.elements + part.modules:
        if not kind.startswith(PASSED):
            box = _placed(assets.out / assets.mesh(kind), position, turn[0] if turn else 0.0)
            if box:
                # what moves needs its room too: a drawbridge's plank stands
                # 9.6 up before it falls
                solid.append(box)
                room.append(box)
    for kind, position, turn in part.marks:
        box = _placed(assets.out / assets.mesh(kind), position, turn)
        if box:
            solid.append(box)
            room.append(_room(box))
    for path in part.railings:
        line = centre_line(path)
        for a, b in zip(line, line[1:]):
            solid.append((min(a[0], b[0]) - RADIUS, max(a[0], b[0]) + RADIUS,
                          min(a[1], b[1]) - RADIUS, max(a[1], b[1]) + RADIUS,
                          min(a[2], b[2]) - RADIUS, max(a[2], b[2]) + RADIUS))
    reach = GAUGE / 2 + TUBE
    for a, b in part.segments:
        # a sloping rail a few units at a time
        steps = max(1, math.ceil(math.dist((a.x, a.z), (b.x, b.z)) / CLASH_RAIL_STEP))
        for k in range(steps):
            p, q = ([a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t]
                    for t in (k / steps, (k + 1) / steps))
            # the tubes reach across the rail, not along it
            rx = reach if abs(b.z - a.z) > 1e-6 else TUBE
            rz = reach if abs(b.x - a.x) > 1e-6 else TUBE
            box = (min(p[0], q[0]) - rx, max(p[0], q[0]) + rx, min(p[1], q[1]) - TUBE,
                   max(p[1], q[1]) + TUBE, min(p[2], q[2]) - rz, max(p[2], q[2]) + rz)
            solid.append(box)
            room.append((box[0], box[1], box[2], max(p[1], q[1]) + CLASH_RAIL_ROOM,
                         box[4], box[5]))
    return solid, room


def _overlap(a: Box, b: Box) -> float:
    """How far two boxes overlap, across (the lesser of X and Z) if they
    overlap up and down too, else 0."""
    dx = min(a[1], b[1]) - max(a[0], b[0])
    dy = min(a[3], b[3]) - max(a[2], b[2])
    dz = min(a[5], b[5]) - max(a[4], b[4])
    return min(dx, dz) if dy > 0 and dx > 0 and dz > 0 else 0.0


def _name(part: Part) -> str:
    if part.branch:
        return (f"sector {part.sector}'s piece {part.branch} ({part.what}) of the branch off "
                f"piece {part.index}")
    if part.index:
        return f"sector {part.sector}'s piece {part.index} ({part.what})"
    return f"sector {part.sector}'s {part.what}"


def _neighbours(parts: list[Part], a: Part, b: Part) -> bool:
    """Whether one part goes on from the other (or a branch comes back to
    it) with only rails and falls between them."""
    before: dict[int, list[int]] = {}
    for part in parts:
        if part.after is not None:
            before.setdefault(part.step, []).append(part.after)
        if part.joins is not None:
            before.setdefault(part.joins, []).append(part.step)
    for later, earlier in ((a, b), (b, a)):
        steps = list(before.get(later.step, []))
        while steps:
            step = steps.pop()
            if step == earlier.step:
                return True
            if parts[step].what in FLOWN:
                steps += before.get(step, [])
    return a is b


def clashes(layout: Layout, assets: Assets) -> list[str]:
    """What is in the ball's way, one line for each pair of pieces."""
    boxes = [(part, *_boxes(part, assets)) for part in layout.parts]
    found = []
    for part, solid, _ in boxes:
        for other, _, room in boxes:
            if _neighbours(layout.parts, part, other):
                continue
            hits = [(a, b) for a in solid for b in room if _overlap(a, b) > CLASH_OVERLAP]
            if hits:
                a, b = hits[0]
                x = (max(a[0], b[0]) + min(a[1], b[1])) / 2
                z = (max(a[4], b[4]) + min(a[5], b[5])) / 2
                found.append(f"{_name(part)} is in the ball's way over {_name(other)}, near "
                             f"({x:.0f}, {z:.0f})")
    return found
