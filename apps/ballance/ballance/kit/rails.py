"""Ballance rails built the way the original levels build them.

A rail is two tubes whose middles are 3.8 apart, each an octagon 0.35 in
radius with its two lowest faces left out, in the Rail material.  The tubes'
rings stand upright, square to the track: a straight rail has one at each
end, so a slope changes at a kink (a hump has one every few units, as
Level_12's do), and a bend is the originals' one bend, 90
degrees about a point 8.1 to the side of the track's middle (the inner tube
6.2, the outer 10), in nine steps of 10 degrees.  Normals are smooth around
the tube; the two at its open bottom lean as the face beside them does.

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

# The ring's corners, in degrees from the tube's outer side up and over; the
# faces join each to the next but the last to the first, leaving the bottom
# open.  The normals at the open bottom's corners lean as their one face does.
ANGLES = [0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 315.0]
NORMALS = [0.0, 45.0, 90.0, 135.0, 180.0, 202.5, 337.5]
FACES = [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (6, 0)]
# A beam's corners, from its right side up and over, and its faces, all round.
BEAM_ANGLES = [22.5 + 45.0 * k for k in range(8)]
BEAM_FACES = [(k, (k + 1) % 8) for k in range(8)]


class Ring:
    """Where a rail's middle line passes: a point and the level heading
    there, as a unit (dx, dz)."""

    def __init__(self, x: float, y: float, z: float, dx: float, dz: float) -> None:
        self.x, self.y, self.z = x, y, z
        self.dx, self.dz = dx, dz


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
                corners = [((point, (u, v), normal)) for (point, normal, u), v in
                           (quad[t] for t in tri)]
                # outward: as the first corner's normal leans
                mesh.triangle("Rail", corners[0][2], *corners)


def _beam(mesh: Mesh, run: list[Ring]) -> None:
    """The beam along the run's middle."""
    rings = []
    along = 0.0
    for k, r in enumerate(run):
        if k:
            p = run[k - 1]
            along += math.dist((p.x, p.y, p.z), (r.x, r.y, r.z))
        rx, rz = r.dz, -r.dx
        corners = []
        for a in BEAM_ANGLES:
            ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
            corners.append(((r.x + rx * ca * TUBE, r.y + sa * TUBE, r.z + rz * ca * TUBE),
                            (rx * ca, sa, rz * ca), a / 360.0))
        rings.append((corners, along / REPEAT))
    for (a, va), (b, vb) in zip(rings, rings[1:]):
        for i, j in BEAM_FACES:
            quad = [(a[i], va), (a[j], va), (b[j], vb), (b[i], vb)]
            for tri in ((0, 1, 2), (0, 2, 3)):
                corners = [((point, (u, v), normal)) for (point, normal, u), v in
                           (quad[t] for t in tri)]
                mesh.triangle("Rail", corners[0][2], *corners)


def bend(x: float, y: float, z: float, d: tuple[int, int], side: tuple[int, int],
         drop: float = 0.0) -> list[Ring]:
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
        rings.append(Ring(px, y - drop * k / steps, pz, d[0] * c + side[0] * s,
                          d[1] * c + side[1] * s))
    return rings
