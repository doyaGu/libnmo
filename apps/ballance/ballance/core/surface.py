"""Triangle queries for ball-sized passages, without substituting mesh bounds."""

import math
from collections import defaultdict

from .space import bounds, inflate, overlap


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def interpolate(a, b, t):
    return tuple(x + t * (y - x) for x, y in zip(a, b))


def closest_on_segment(point, a, b):
    direction = sub(b, a)
    length = dot(direction, direction)
    t = max(0, min(1, dot(sub(point, a), direction) / length)) if length else 0
    return interpolate(a, b, t)


def inside(point, triangle, normal):
    return all(
        dot(cross(sub(b, a), sub(point, a)), normal) >= -1e-9
        for a, b in zip(triangle, (*triangle[1:], triangle[0]))
    )


def closest_on_triangle(point, triangle):
    a, b, c = triangle
    normal = cross(sub(b, a), sub(c, a))
    length = dot(normal, normal)
    if length > 1e-18:
        offset = dot(sub(point, a), normal) / length
        projected = tuple(p - offset * n for p, n in zip(point, normal))
        if inside(projected, triangle, normal):
            return projected
    return min(
        (closest_on_segment(point, a, b) for a, b in zip(triangle, (*triangle[1:], triangle[0]))),
        key=lambda p: math.dist(point, p),
    )


def segment_contacts(a, b, c, d):
    # Interior closest points and all four boundary cases also handle parallel
    # and zero-length segments without unstable parameter divisions.
    yield a, closest_on_segment(a, c, d)
    yield b, closest_on_segment(b, c, d)
    yield closest_on_segment(c, a, b), c
    yield closest_on_segment(d, a, b), d
    u, v, w = sub(b, a), sub(d, c), sub(a, c)
    uu, uv, vv = dot(u, u), dot(u, v), dot(v, v)
    uw, vw = dot(u, w), dot(v, w)
    determinant = uu * vv - uv * uv
    if determinant > 1e-12 * uu * vv:
        s = (uv * vw - vv * uw) / determinant
        t = (uu * vw - uv * uw) / determinant
        if 0 <= s <= 1 and 0 <= t <= 1:
            yield interpolate(a, b, s), interpolate(c, d, t)


def triangle_contact(a, b, triangle):
    """Closest pair (segment point, triangle point), including degenerate faces."""
    normal = cross(sub(triangle[1], triangle[0]), sub(triangle[2], triangle[0]))
    denominator = dot(sub(b, a), normal)
    if abs(denominator) > 1e-12:
        t = dot(sub(triangle[0], a), normal) / denominator
        point = interpolate(a, b, t)
        if 0 <= t <= 1 and inside(point, triangle, normal):
            return point, point
    candidates = [(a, closest_on_triangle(a, triangle)), (b, closest_on_triangle(b, triangle))]
    for c, d in zip(triangle, (*triangle[1:], triangle[0])):
        candidates.extend(segment_contacts(a, b, c, d))
    return min(candidates, key=lambda pair: math.dist(*pair))


def segment_triangle(a, b, triangle):
    return math.dist(*triangle_contact(a, b, triangle))


class Surfaces:
    """XZ-indexed world triangles; holes and sloping surfaces remain intact."""

    def __init__(self, records, cell=10):
        self.records = list(records)
        self.boxes = [bounds(triangle) for _, triangle in self.records]
        self.cell = cell
        self.buckets = defaultdict(list)
        for index, (_, triangle) in enumerate(self.records):
            for key in self.keys(bounds(triangle)):
                self.buckets[key].append(index)

    def keys(self, box):
        for x in range(math.floor(box[0] / self.cell), math.floor(box[1] / self.cell) + 1):
            for z in range(math.floor(box[4] / self.cell), math.floor(box[5] / self.cell) + 1):
                yield x, z

    def nearby(self, box):
        indices = {i for key in self.keys(box) for i in self.buckets.get(key, ())}
        return (self.records[i] for i in sorted(indices) if overlap(box, self.boxes[i]))

    def clearance(self, a, b, radius, tolerance=0.05):
        hits = {}
        for owner, triangle in self.nearby(inflate(bounds((a, b)), radius)):
            distance = segment_triangle(a, b, triangle)
            if distance < radius - tolerance:
                penetration = radius - distance
                hits[owner] = max(hits.get(owner, 0), penetration)
        return hits

    def supporting_contacts(self, a, b, radius):
        """Whether every penetrating face can support the sphere from below."""
        for _, triangle in self.nearby(inflate(bounds((a, b)), radius)):
            centre, contact = triangle_contact(a, b, triangle)
            if math.dist(centre, contact) < radius - 0.05 and contact[1] > centre[1] + 0.05:
                return False
        return True

    def support(self, point, tolerance=0.25):
        """Up-facing floor at the requested height, never another layer below."""
        x, y, z = point
        box = (x, x, y - tolerance, y + tolerance, z, z)
        for owner, triangle in self.nearby(box):
            normal = cross(sub(triangle[1], triangle[0]), sub(triangle[2], triangle[0]))
            length = math.sqrt(dot(normal, normal))
            if not length or normal[1] / length < 0.6:
                continue
            height = (
                triangle[0][1]
                - (normal[0] * (x - triangle[0][0]) + normal[2] * (z - triangle[0][2])) / normal[1]
            )
            if abs(height - y) <= tolerance and inside((x, height, z), triangle, normal):
                return owner
        return None
