"""Ballance rails built the way the original levels build them.

A rail is two tubes whose middles are 3.8 apart, each an octagon 0.35 in
radius with its two lowest faces left out, in the Rail material.  The tubes'
rings stand upright, square to the track: a straight rail has one at each
end, so a slope changes at a kink, and a bend is the originals' one bend, 90
degrees about a point 8.1 to the side of the track's middle (the inner tube
6.2, the outer 10), in nine steps of 10 degrees.  Normals are smooth around
the tube; the two at its open bottom lean as the face beside them does.

Where a rail meets a floor it stops at the floor's edge, open, with the tubes'
middles 0.14 above the bottom of the V the floor opens to it there
(ballance_floors.Floors.dock()).
"""

from __future__ import annotations

import math
from pathlib import Path

GAUGE = 3.8
TUBE = 0.35
BEND_RADIUS = 8.1
BEND_STEP = 10.0
ABOVE_V = 0.14
REPEAT = 5.0

# The ring's corners, in degrees from the tube's outer side up and over; the
# faces join each to the next but the last to the first, leaving the bottom
# open.  The normals at the open bottom's corners lean as their one face does.
ANGLES = [0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 315.0]
NORMALS = [0.0, 45.0, 90.0, 135.0, 180.0, 202.5, 337.5]
FACES = [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (6, 0)]


class Ring:
    """Where a rail's middle line passes: a point and the level heading
    there, as a unit (dx, dz)."""

    def __init__(self, x: float, y: float, z: float, dx: float, dz: float) -> None:
        self.x, self.y, self.z = x, y, z
        self.dx, self.dz = dx, dz


class Rails:
    """The rails of one floor object, each a list of rings from end to end."""

    def __init__(self) -> None:
        self.runs: list[list[Ring]] = []

    def write(self, path: Path) -> None:
        lines = ["usemtl Rail"]
        count = 0
        for run in self.runs:
            for side in (-1.0, 1.0):
                count = self._tube(lines, run, side, count)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    @staticmethod
    def _tube(lines: list[str], run: list[Ring], side: float, count: int) -> int:
        """Writes the tube side * GAUGE / 2 to the right of the run's middle."""
        rings = []
        along = 0.0
        for k, r in enumerate(run):
            if k:
                p = run[k - 1]
                along += math.dist((p.x, p.y, p.z), (r.x, r.y, r.z))
            # to the right of heading (dx, dz), as Virtools' left-handed Y-up
            # space has it: (dz, -dx)
            rx, rz = r.dz, -r.dx
            cx, cz = r.x + rx * side * GAUGE / 2, r.z + rz * side * GAUGE / 2
            corners = []
            for a, n in zip(ANGLES, NORMALS):
                # angle 0 points away from the other tube
                ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
                cn, sn = math.cos(math.radians(n)), math.sin(math.radians(n))
                corners.append(((cx + rx * side * ca * TUBE, r.y + sa * TUBE,
                                 cz + rz * side * ca * TUBE),
                                (rx * side * cn, sn, rz * side * cn), a / 360.0))
            rings.append((corners, along / REPEAT))
        for (a, va), (b, vb) in zip(rings, rings[1:]):
            for i, j in FACES:
                quad = [(a[i], va), (a[j], va), (b[j], vb), (b[i], vb)]
                for tri in ((0, 1, 2), (0, 2, 3)):
                    corners = [quad[t] for t in tri]
                    if not _outward(corners):
                        corners = [corners[0], corners[2], corners[1]]
                    for (point, normal, u), v in corners:
                        lines.append("v {:g} {:g} {:g}".format(*point))
                        lines.append(f"vt {u:g} {v:g}")
                        lines.append("vn {:g} {:g} {:g}".format(*normal))
                    refs = [count + 1, count + 2, count + 3]
                    lines.append("f " + " ".join(f"{r}/{r}/{r}" for r in refs))
                    count += 3
        return count


def _outward(corners) -> bool:
    (a, na, _), _ = corners[0]
    (b, _, _), _ = corners[1]
    (c, _, _), _ = corners[2]
    ab = [b[i] - a[i] for i in range(3)]
    ac = [c[i] - a[i] for i in range(3)]
    normal = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
              ab[0] * ac[1] - ab[1] * ac[0])
    return sum(normal[i] * na[i] for i in range(3)) >= 0


def bend(x: float, y: float, z: float, d: tuple[int, int], side: tuple[int, int]) -> list[Ring]:
    """The rings of the originals' bend, from (x, y, z) heading d, turning
    toward side (a unit heading square to d)."""
    cx, cz = x + side[0] * BEND_RADIUS, z + side[1] * BEND_RADIUS
    rings = []
    steps = round(90.0 / BEND_STEP)
    for k in range(steps + 1):
        t = math.radians(k * BEND_STEP)
        c, s = math.cos(t), math.sin(t)
        px = cx - side[0] * BEND_RADIUS * c + d[0] * BEND_RADIUS * s
        pz = cz - side[1] * BEND_RADIUS * c + d[1] * BEND_RADIUS * s
        rings.append(Ring(px, y, pz, d[0] * c + side[0] * s, d[1] * c + side[1] * s))
    return rings
