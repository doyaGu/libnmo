"""Headings and points on the XZ plane, as Virtools' left-handed, Y-up space
has them.

A heading is a horizontal unit vector (dx, dz); grid-based pieces require
one of the four cardinal headings. A point is (x, z). Looking along a
heading with Y up, to its right is (dz, -dx): to the right of -X is +Z.
"""

from __future__ import annotations

import math

Heading = tuple[float, float]
Point = tuple[float, float]

HEADINGS: dict[str, Heading] = {"+x": (1, 0), "-x": (-1, 0), "+z": (0, 1), "-z": (0, -1)}


def right_of(d: Heading) -> Heading:
    """The heading to the right of d."""
    return d[1], -d[0]


def left_of(d: Heading) -> Heading:
    """The heading to the left of d."""
    return -d[1], d[0]


def back_of(d: Heading) -> Heading:
    """The heading opposite d."""
    return -d[0], -d[1]


def side_of(d: Heading, side: str) -> Heading:
    """The heading to the "left" or "right" of d."""
    return right_of(d) if side == "right" else left_of(d)


def turn_of(d: Heading) -> float:
    """The turn about Y, in degrees, that points an object's X axis along d
    (90 turns X to -Z)."""
    return math.degrees(math.atan2(-d[1], d[0])) % 360


def axis_of(d: Heading) -> str:
    """"x" or "z": the axis d runs along."""
    return "x" if d[0] else "z"


def at(p: Point, d: Heading, along: float, right: float = 0.0) -> Point:
    """The point along d from p, and to its right."""
    r = right_of(d)
    return p[0] + d[0] * along + r[0] * right, p[1] + d[1] * along + r[1] * right


def rect(p: Point, d: Heading, length: float, width: float) -> tuple[float, float, float, float]:
    """The corners (x0, z0, x1, z1) of the rectangle length long along d from
    p and width wide, its middle line through p."""
    a = at(p, d, 0, -width / 2)
    b = at(p, d, length, width / 2)
    return a[0], a[1], b[0], b[1]


def across(p: Point, d: Heading, width: float) -> tuple[float, float, float, float]:
    """The ends (x0, z0, x1, z1) of the line width wide across d through p."""
    a, b = at(p, d, 0, -width / 2), at(p, d, 0, width / 2)
    return a[0], a[1], b[0], b[1]
