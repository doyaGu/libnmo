"""Ballance rails built the way the original levels build them.

A rail is two tubes whose middles are 3.8 apart, each an octagon 0.35 in
radius, whole and capped at its ends (the originals leave the two lowest
faces out and the ends open), in the Rail material.  The tubes'
rings stand upright, square to the track: a straight rail has one at each
end, so a slope changes at a kink (a hump has one every few units, as
Level_12's do), and a bend is the originals' one bend, 90
degrees about a point 8.1 to the side of the track's middle (the inner tube
6.2, the outer 10), in nine steps of 10 degrees.  Normals are smooth around
the tube.

Where a rail meets a floor it stops at the floor's edge, open, with the tubes'
middles 0.14 above the bottom of the V the floor opens to it there
(ballance.kit.floors.Floors.dock()).

A beam (Level_12's) is one tube the ball rolls along on top of: the same
octagon 0.35 in radius, whole, turned so that a face lies flat on top.
"""

from __future__ import annotations

import math
from pathlib import Path

from ..core.obj import Mesh

GAUGE = 3.8
TUBE = 0.35
BEND_RADIUS = 8.1
BEND_STEP = 10.0
ABOVE_V = 0.14
REPEAT = 5.0

# The ring's corners, in degrees from the tube's outer side up and over, and
# its faces, all round.  The originals leave the two lowest faces out, as
# their camera never looks up at a rail; levels that stack their tracks
# show the open half pipe from below.
ANGLES = [45.0 * k for k in range(8)]
NORMALS = ANGLES
FACES = [(k, (k + 1) % 8) for k in range(8)]
# A beam's corners, from its right side up and over, and its faces, all round.
BEAM_ANGLES = [22.5 + 45.0 * k for k in range(8)]
BEAM_FACES = [(k, (k + 1) % 8) for k in range(8)]


class Ring:
    """Where a rail's middle line passes: a point and the level heading
    there, as a unit (dx, dz)."""

    def __init__(
        self,
        x: float,
        y: float,
        z: float,
        dx: float,
        dz: float,
        bank: float = 0.0,
        gauge: float = GAUGE,
        radius: float = TUBE,
    ) -> None:
        self.x, self.y, self.z = x, y, z
        self.dx, self.dz = dx, dz
        self.bank, self.gauge, self.radius = bank, gauge, radius

    def axes(self):
        """Banked right and up vectors; positive bank raises the right tube."""
        c, s = math.cos(math.radians(self.bank)), math.sin(math.radians(self.bank))
        return (self.dz * c, s, -self.dx * c), (-self.dz * s, c, self.dx * s)


class Beam(list):
    """A run of rings a beam passes through, rather than a rail."""


class Rails:
    """The rails of one floor object, each a list of rings from end to end
    (a Beam for a beam)."""

    def __init__(self) -> None:
        self.runs: list[list[Ring]] = []

    def write(self, path: Path) -> None:
        mesh = Mesh()
        for run in self.runs:
            if isinstance(run, Beam):
                _beam(mesh, run)
                continue
            for side in (-1.0, 1.0):
                _tube(mesh, run, side)
        mesh.write(path)


def _tube(mesh: Mesh, run: list[Ring], side: float) -> None:
    """The tube side * GAUGE / 2 to the right of the run's middle."""
    rings = []
    along = 0.0
    for k, r in enumerate(run):
        if k:
            p = run[k - 1]
            along += math.dist((p.x, p.y, p.z), (r.x, r.y, r.z))
        # to the right of heading (dx, dz), as Virtools' left-handed Y-up
        # space has it: (dz, -dx)
        right, up = r.axes()
        centre = tuple(v + side * r.gauge / 2 * q for v, q in zip((r.x, r.y, r.z), right))
        corners = []
        for a, n in zip(ANGLES, NORMALS):
            # angle 0 points away from the other tube
            ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
            cn, sn = math.cos(math.radians(n)), math.sin(math.radians(n))
            corners.append(
                (
                    tuple(
                        centre[j] + r.radius * (right[j] * side * ca + up[j] * sa) for j in range(3)
                    ),
                    tuple(right[j] * side * cn + up[j] * sn for j in range(3)),
                    a / 360.0,
                )
            )
        rings.append((corners, along / REPEAT))
    _caps(mesh, [corners for corners, _ in rings])
    for (a, va), (b, vb) in zip(rings, rings[1:]):
        for i, j in FACES:
            quad = [(a[i], va), (a[j], va), (b[j], vb), (b[i], vb)]
            for tri in ((0, 1, 2), (0, 2, 3)):
                corners = [
                    ((point, (u, v), normal)) for (point, normal, u), v in (quad[t] for t in tri)
                ]
                # outward: as the first corner's normal leans
                mesh.triangle('Rail', corners[0][2], *corners)


def _beam(mesh: Mesh, run: list[Ring]) -> None:
    """The beam along the run's middle."""
    rings = []
    along = 0.0
    for k, r in enumerate(run):
        if k:
            p = run[k - 1]
            along += math.dist((p.x, p.y, p.z), (r.x, r.y, r.z))
        right, up = r.axes()
        corners = []
        for a in BEAM_ANGLES:
            ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
            normal = tuple(right[j] * ca + up[j] * sa for j in range(3))
            corners.append(
                (
                    tuple(v + r.radius * n for v, n in zip((r.x, r.y, r.z), normal)),
                    normal,
                    a / 360.0,
                )
            )
        rings.append((corners, along / REPEAT))
    _caps(mesh, [corners for corners, _ in rings])
    for (a, va), (b, vb) in zip(rings, rings[1:]):
        for i, j in BEAM_FACES:
            quad = [(a[i], va), (a[j], va), (b[j], vb), (b[i], vb)]
            for tri in ((0, 1, 2), (0, 2, 3)):
                corners = [
                    ((point, (u, v), normal)) for (point, normal, u), v in (quad[t] for t in tri)
                ]
                mesh.triangle('Rail', corners[0][2], *corners)


def _caps(mesh: Mesh, rings: list[list[tuple]]) -> None:
    """Closes a tube's two ends, each with its ring's octagon, facing out
    along the tube: a rail or a beam ending over a beam or in the air shows
    its end."""
    if len(rings) < 2:
        return
    for ring, inner in ((rings[0], rings[1]), (rings[-1], rings[-2])):
        points = [point for point, _, _ in ring]
        centre = tuple(sum(p[j] for p in points) / len(points) for j in range(3))
        inside = tuple(sum(p[j] for p, _, _ in inner) / len(inner) for j in range(3))
        out = tuple(c - i for c, i in zip(centre, inside))
        length = math.sqrt(sum(v * v for v in out)) or 1.0
        out = tuple(v / length for v in out)
        for k in range(len(points)):
            a, b = points[k], points[(k + 1) % len(points)]
            mesh.triangle(
                'Rail', out, (centre, (0.5, 0.5), out), (a, (0.0, 0.0), out), (b, (1.0, 0.0), out)
            )


def bend(
    x: float, y: float, z: float, d: tuple[int, int], side: tuple[int, int], drop: float = 0.0
) -> list[Ring]:
    """The rings of the originals' bend, from (x, y, z) heading d, turning
    toward side (a unit heading square to d), coming down drop along it."""
    cx, cz = x + side[0] * BEND_RADIUS, z + side[1] * BEND_RADIUS
    rings = []
    steps = round(90.0 / BEND_STEP)
    for k in range(steps + 1):
        t = math.radians(k * BEND_STEP)
        c, s = math.cos(t), math.sin(t)
        px = cx - side[0] * BEND_RADIUS * c + d[0] * BEND_RADIUS * s
        pz = cz - side[1] * BEND_RADIUS * c + d[1] * BEND_RADIUS * s
        rings.append(
            Ring(px, y - drop * k / steps, pz, d[0] * c + side[0] * s, d[1] * c + side[1] * s)
        )
    return rings
