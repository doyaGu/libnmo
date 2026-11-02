"""Railings built the way the original levels build them (Level_01's
A01_Rail_04 and the rest): one tube, an octagon 0.245 in radius all round,
in the Rail material, standing just outside a floor's walls.  It rises from
a post 1.77 below the floor's rim to 2.855 above it, runs along the walls
there and comes down again in another post, turning every corner in a bend
1.06 about; clamps hold the posts' feet to the walls.  As on the originals'
rails, its UVs are of no account: the game maps an environment texture on
it as it plays.
"""

from __future__ import annotations

import math

from ..core.obj import Mesh

RADIUS = 0.245
SIDES = 8
TOP = 2.855      # the tube's middle over the rim
FOOT = 1.77      # the posts' feet under it
BEND = 1.06      # the bends' radius
OUT = 0.24       # the tube's middle outside the walls
BEND_STEPS = 6   # steps to a quarter turn

Point3 = tuple[float, float, float]
ORIGIN: Point3 = (0.0, 0.0, 0.0)


def _sub(a: Point3, b: Point3) -> Point3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a: Point3, b: Point3, k: float = 1.0) -> Point3:
    return (a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k)


def _unit(a: Point3) -> Point3:
    n = math.sqrt(sum(v * v for v in a)) or 1.0
    return (a[0] / n, a[1] / n, a[2] / n)


def _cross(a: Point3, b: Point3) -> Point3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a: Point3, b: Point3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def centre_line(path: list[Point3]) -> list[Point3]:
    """The tube's middle along a railing over path, the points (x, rim, z)
    its top runs over: up from the first's foot, along, and down to the
    last's, each corner rounded BEND about."""
    corners = [(path[0][0], path[0][1] - FOOT, path[0][2])]
    corners += [(x, rim + TOP, z) for x, rim, z in path]
    corners.append((path[-1][0], path[-1][1] - FOOT, path[-1][2]))
    line = [corners[0]]
    for a, b, c in zip(corners, corners[1:], corners[2:]):
        u, v = _unit(_sub(a, b)), _unit(_sub(c, b))
        angle = math.acos(max(-1.0, min(1.0, _dot(u, v))))
        if angle > math.pi - 1e-6:
            line.append(b)
            continue
        # the arc touching both legs, BEND from its middle
        back = BEND / math.tan(angle / 2)
        start, end = _add(b, u, back), _add(b, v, back)
        middle = _add(b, _unit(_add(u, v)), BEND / math.sin(angle / 2))
        turn = math.pi - angle
        steps = max(1, round(BEND_STEPS * turn / (math.pi / 2)))
        r0, r1 = _sub(start, middle), _sub(end, middle)
        for k in range(steps + 1):
            t = k / steps
            # slerp between the two radii
            w0 = math.sin((1 - t) * turn) / math.sin(turn)
            w1 = math.sin(t * turn) / math.sin(turn)
            line.append(_add(_add(middle, r0, w0), r1, w1))
    line.append(corners[-1])
    return line


def railing(mesh: Mesh, path: list[Point3]) -> None:
    """A railing over path (see centre_line), into mesh."""
    line = centre_line(path)
    rings = []
    normal: Point3 | None = None
    for k, p in enumerate(line):
        ahead = _sub(line[min(k + 1, len(line) - 1)], line[max(k - 1, 0)])
        t = _unit(ahead)
        if normal is None:
            seed = (1.0, 0.0, 0.0) if abs(t[0]) < 0.9 else (0.0, 0.0, 1.0)
            normal = _unit(_cross(t, seed))
        # carried along the tube, kept square to it
        normal = _unit(_add(normal, t, -_dot(normal, t)))
        side = _cross(t, normal)
        ring = []
        for i in range(SIDES):
            a = 2 * math.pi * i / SIDES
            n = _add(_add(ORIGIN, normal, math.cos(a)), side, math.sin(a))
            ring.append((_add(p, n, RADIUS), n))
        rings.append(ring)
    for ra, rb in zip(rings, rings[1:]):
        for i in range(SIDES):
            j = (i + 1) % SIDES
            quad = [ra[i], ra[j], rb[j], rb[i]]
            corners = [(point, (0.0, 0.0), n) for point, n in quad]
            mesh.quad("Rail", quad[0][1], *corners)
