"""Points for `nmo behavior run --steer`: "X,Z[,Y[,F[,NAME,WX,WZ,R]]]"
joined by ";".

A point with a height Y is passed only once the ball is that high there; one
with frames F only after it has pushed toward it that long; one with an
object NAME only once that object is within R of WX,WZ (a swing's seat come
round); at any of them, the autopilot stops the ball.  One whose F is "rush"
is run at flat out, never braking (a jump's run-up). "precision" follows
narrow beams slowly with a smaller tracking tolerance. A found path becomes the fewest points
that stay
within TOLERANCE of it (Ramer-Douglas-Peucker, on X and Z).
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from .terrain import Point3

TOLERANCE = 0.4


@dataclass
class SteerPoint:
    x: float
    z: float
    y: float | None = None  # the height to wait for
    hold: int = 0  # the frames to keep pushing there; -1: rush to it
    wait: tuple[str, float, float, float] | None = None  # an object, where, within
    precision: bool = False

    def __str__(self) -> str:
        text = f'{self.x:g},{self.z:g}'
        if self.y is not None or self.hold or self.wait or self.precision:
            text += ',' + (f'{self.y:g}' if self.y is not None else '')
        if self.hold or self.wait or self.precision:
            text += ',' + (
                'precision'
                if self.precision
                else 'rush'
                if self.hold < 0
                else f'{self.hold}'
                if self.hold
                else ''
            )
        if self.wait:
            name, x, z, reach = self.wait
            text += f',{name},{x:g},{z:g},{reach:g}'
        return text


def parse(text: str) -> list[SteerPoint]:
    """Points as --steer takes them, without the "@FRAME"."""
    points = []
    for part in text.split(';'):
        fields = part.split(',')
        y = float(fields[2]) if len(fields) > 2 and fields[2] else None
        wait = (
            (fields[4], float(fields[5]), float(fields[6]), float(fields[7]))
            if len(fields) > 7
            else None
        )
        precision = len(fields) > 3 and fields[3] == 'precision'
        hold = (
            0
            if precision
            else -1
            if len(fields) > 3 and fields[3] == 'rush'
            else int(fields[3])
            if len(fields) > 3 and fields[3]
            else 0
        )
        points.append(SteerPoint(float(fields[0]), float(fields[1]), y, hold, wait, precision))
    return points


def format_points(points: list[SteerPoint]) -> str:
    return ';'.join(str(p) for p in points)


def simplify(path: list[Point3]) -> list[SteerPoint]:
    """The path's points that keep it within TOLERANCE, and where its height
    turns (more than 1.5 off the straight between)."""
    return [SteerPoint(p[0], p[2]) for p in _simplified(path)]


def _simplified(path: list[Point3]) -> list[Point3]:
    if len(path) < 3:
        return path
    a, b = path[0], path[-1]
    length = math.hypot(b[0] - a[0], b[2] - a[2]) or 1e-9
    worst, at = 0.0, 0
    for k in range(1, len(path) - 1):
        p = path[k]
        off = abs((b[0] - a[0]) * (a[2] - p[2]) - (a[0] - p[0]) * (b[2] - a[2])) / length
        if abs(p[1] - (a[1] + (b[1] - a[1]) * k / (len(path) - 1))) > 1.5:
            off = max(off, TOLERANCE * 2)
        if off > worst:
            worst, at = off, k
    if worst <= TOLERANCE:
        return [a, b]
    return _simplified(path[: at + 1])[:-1] + _simplified(path[at:])


def fix(
    points: list[SteerPoint],
    box: tuple[float, float, float, float],
    replacement: list[SteerPoint],
    run: int = 0,
) -> list[SteerPoint]:
    """The points with a run of those in box (x0, x1, z0, z1) replaced: the
    first run, or the one run picks (-1 the last)."""
    x0, x1, z0, z1 = box
    inside = [k for k, p in enumerate(points) if x0 <= p.x <= x1 and z0 <= p.z <= z1]
    if not inside:
        raise ValueError(f'no points in {box}')
    runs = [[inside[0]]]
    for k in inside[1:]:
        if k == runs[-1][-1] + 1:
            runs[-1].append(k)
        else:
            runs.append([k])
    chosen = runs[run]
    return points[: chosen[0]] + replacement + points[chosen[-1] + 1 :]
