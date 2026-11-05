"""What is drawn over a level's views: a route, a ball's path, and the
names of the mechanisms.  Each overlay draws itself through a Camera, and
only where it lies within the camera's bounds."""

from __future__ import annotations

from typing import Callable

import numpy as np
from PIL import ImageDraw

from ..game.catalog import Mechanism, mechanism_of
from .render import Camera
from .scene import SceneObject

ROUTE, STOP, TRACE_MARK, TRACE_TEXT = (
    (255, 230, 60),
    (255, 80, 80),
    (255, 255, 255),
    (200, 220, 255),
)
Point3 = tuple[float, float, float]


class Line:
    def __init__(self, points: list[Point3], colour: tuple, width: int) -> None:
        self.points, self.colour, self.width = points, colour, width

    def draw(self, draw: ImageDraw.ImageDraw, camera: Camera) -> None:
        for a, b in zip(self.points, self.points[1:]):
            if camera.within(a) or camera.within(b):
                draw.line([camera.pixel(a), camera.pixel(b)], fill=self.colour, width=self.width)


class Dot:
    def __init__(self, point: Point3, colour: tuple, radius: float) -> None:
        self.point, self.colour, self.radius = point, colour, radius

    def draw(self, draw: ImageDraw.ImageDraw, camera: Camera) -> None:
        if camera.within(self.point):
            x, y = camera.pixel(self.point)
            r = self.radius
            draw.ellipse([x - r, y - r, x + r, y + r], outline=self.colour, width=2)


class Text:
    def __init__(self, point: Point3, colour: tuple, text: str) -> None:
        self.point, self.colour, self.text = point, colour, text

    def draw(self, draw: ImageDraw.ImageDraw, camera: Camera) -> None:
        if camera.within(self.point):
            x, y = camera.pixel(self.point)
            draw.text((x + 4, y - 12), self.text, fill=self.colour)


class Label(Text):
    """Text in a box, with a line to the point it names."""

    def draw(self, draw: ImageDraw.ImageDraw, camera: Camera) -> None:
        if camera.within(self.point):
            x, y = camera.pixel(self.point)
            box = draw.textbbox((x + 6, y - 14), self.text)
            draw.rectangle(
                [box[0] - 2, box[1] - 1, box[2] + 2, box[3] + 1],
                fill=(10, 10, 14),
                outline=self.colour,
            )
            draw.text((x + 6, y - 14), self.text, fill=self.colour)
            draw.line([(x, y), (box[0] - 2, box[3])], fill=self.colour, width=1)


def route_overlays(route: list[list], floor_y: Callable[[float, float], float]) -> list:
    """A route_points.json: the path along its points, numbered; the points
    the ball stops at (a height to be lifted to, frames to hold, an object to
    wait for) ringed."""
    out, points = [], []
    for i, point in enumerate(route):
        x, z = point[0], point[1]
        p = (x, floor_y(x, z), z)
        points.append(p)
        rush = len(point) > 3 and point[3] == 'rush'
        precise = len(point) > 3 and point[3] == 'precision'
        stop = len(point) > 2 and not (rush or precise)
        out.append(Dot(p, STOP if stop else ROUTE, 6 if stop else 3))
        label = str(i)
        if rush:
            label += ' rush'
        elif precise:
            label += ' precision'
        elif len(point) > 4:
            label += f' wait {point[4]}'
        elif len(point) > 3 and isinstance(point[3], str) and point[3].startswith('push:'):
            label += f' push {point[3][5:]}'
        elif len(point) > 3 and point[3]:
            label += f' hold {point[3]:g}'
        elif len(point) > 2 and point[2] is not None:
            label += f' wait y {point[2]:g}'
        out.append(Text(p, ROUTE, label))
    return [Line(points, ROUTE, 2)] + out


def trace_overlays(rows: list[dict]) -> list:
    """A ball's path from a --steer-log, from slow (blue) to fast (red), with
    the frame every 100 frames."""
    out = []
    speeds = [float(r.get('speed', 0) or 0) for r in rows]
    top = max(max(speeds, default=0.0), 1e-6)
    for a, b, speed in zip(rows, rows[1:], speeds):
        t = min(speed / top, 1.0)
        colour = (int(60 + 195 * t), int(90 * (1 - t) + 40), int(255 * (1 - t)))
        out.append(
            Line(
                [
                    (float(a['x']), float(a['y']), float(a['z'])),
                    (float(b['x']), float(b['y']), float(b['z'])),
                ],
                colour,
                3,
            )
        )
    for row in rows:
        if int(row['frame']) % 100 == 0:
            p = (float(row['x']), float(row['y']), float(row['z']))
            out.append(Dot(p, TRACE_MARK, 2))
            out.append(Text(p, TRACE_TEXT, row['frame']))
    return out


def sector_of(obj: SceneObject) -> str:
    """ "S2" for an object in Sector_02, or ""."""
    for group in obj.groups:
        if group.startswith('Sector_'):
            return 'S' + group[len('Sector_') :].lstrip('0')
    return ''


def mechanism_labels(
    scene: list[SceneObject], box: tuple[float, float, float, float]
) -> tuple[list[Label], dict[Mechanism, int]]:
    """The mechanisms, elements and markers within box (x0, x1, z0, z1),
    named over their tops, and how many of each there are."""
    x0, x1, z0, z1 = box
    labels, present = [], {}
    for obj in scene:
        mechanism = mechanism_of(obj.name, obj.groups)
        if mechanism is None or not obj.triangles:
            continue
        pts = np.array([p for t in obj.triangles for p in t.points])
        cx, cz = float(pts[:, 0].mean()), float(pts[:, 2].mean())
        if not (x0 <= cx <= x1 and z0 <= cz <= z1):
            continue
        sector = sector_of(obj)
        labels.append(
            Label(
                (cx, float(pts[:, 1].max()) + 0.5, cz),
                mechanism.colour,
                mechanism.name + (f' {sector}' if sector else ''),
            )
        )
        present[mechanism] = present.get(mechanism, 0) + 1
    return labels, present
