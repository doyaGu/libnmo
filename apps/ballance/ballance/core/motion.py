"""Continuous rigid rotation bounds, including extrema between samples."""

import math
from itertools import product, combinations

from .space import bounds


def corners(box):
    return list(product(*[box[k : k + 2] for k in (0, 2, 4)]))


def place(point, position, yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    x, y, z = point
    return (position[0] + x * c + z * s, position[1] + y, position[2] - x * s + z * c)


def rotate(point, pivot, axis, angle):
    """Rodrigues rotation about a unit axis, in the module frame."""
    v = [p - q for p, q in zip(point, pivot)]
    c, s = math.cos(angle), math.sin(angle)
    dot = sum(a * b for a, b in zip(axis, v))
    cross = (
        axis[1] * v[2] - axis[2] * v[1],
        axis[2] * v[0] - axis[0] * v[2],
        axis[0] * v[1] - axis[1] * v[0],
    )
    return tuple(pivot[k] + v[k] * c + cross[k] * s + axis[k] * dot * (1 - c) for k in range(3))


def rotation_boxes(points, pivot, axis, position=(0, 0, 0), yaw=0, start=-180, end=180, step=10):
    """Exact coordinate extrema for each angular interval, then world AABBs.

    Subdivision reduces empty space; it never leaves gaps between poses.
    Bounds contain the convex hull of the supplied points at every angle.
    """
    if (
        not points
        or len(pivot) != 3
        or len(axis) != 3
        or len(position) != 3
        or any(len(p) != 3 for p in points)
        or not all(
            math.isfinite(v)
            for v in [
                *pivot,
                *axis,
                *position,
                yaw,
                start,
                end,
                step,
                *[v for p in points for v in p],
            ]
        )
        or abs(sum(v * v for v in axis) - 1) > 1e-6
        or not 0 < step <= 180
        or not 0 <= end - start <= 360
    ):
        raise ValueError(
            'rotation needs finite points, a unit axis and an ordered range within 360 degrees'
        )
    coefficients = []
    end, start = end - start + start % 360, start % 360
    for p in points:
        q0, q90, q180 = [
            place(rotate(p, pivot, axis, a), position, yaw) for a in (0, math.pi / 2, math.pi)
        ]
        coefficients.append(
            [
                ((q0[k] - q180[k]) / 2, q90[k] - (q0[k] + q180[k]) / 2, (q0[k] + q180[k]) / 2)
                for k in range(3)
            ]
        )
    count = max(1, math.ceil((end - start) / step))
    result = []
    for i in range(count):
        lo, hi = [math.radians(start + (end - start) * j / count) for j in (i, i + 1)]
        box = []
        for k in range(3):
            values = []
            for coefficients_at_point in coefficients:
                a, b, c = coefficients_at_point[k]
                phase = math.atan2(b, a)
                angles = [lo, hi] + [
                    phase + n * math.pi
                    for n in range(
                        math.ceil((lo - phase) / math.pi), math.floor((hi - phase) / math.pi) + 1
                    )
                ]
                values.extend(a * math.cos(t) + b * math.sin(t) + c for t in angles)
            box.extend((min(values), max(values)))
        result.append(tuple(box))
    return result


def segment_face(a, b, triangle, margin=0.05):
    """Prove a convex body's interior chord crosses a static triangle.

    Both endpoints must penetrate the face plane by margin. AABB overlap
    alone is never a proof that a hinge has a physical stop.
    """
    p, q, r = triangle
    u = [y - x for x, y in zip(p, q)]
    v = [y - x for x, y in zip(p, r)]
    normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = math.sqrt(sum(x * x for x in normal))
    if length < 1e-9:
        return None
    da, db = [sum((x - y) * n for x, y, n in zip(point, p, normal)) / length for point in (a, b)]
    if not min(da, db) < -margin or not max(da, db) > margin:
        return None
    t = da / (da - db)
    point = tuple(x + t * (y - x) for x, y in zip(a, b))
    uu, vv, uv = sum(x * x for x in u), sum(x * x for x in v), sum(x * y for x, y in zip(u, v))
    w = [x - y for x, y in zip(point, p)]
    wu, wv = sum(x * y for x, y in zip(w, u)), sum(x * y for x, y in zip(w, v))
    denominator = uu * vv - uv * uv
    if denominator < 1e-12:
        return None
    s, t = (wu * vv - wv * uv) / denominator, (wv * uu - wu * uv) / denominator
    weights = (1 - s - t, s, t)
    edges = (math.dist(q, r), math.dist(p, r), math.dist(p, q))
    return (
        point
        if all(weight * length / edge > margin for weight, edge in zip(weights, edges))
        else None
    )


def hinge_stops(hulls, pivot, axis, faces, step=5):
    """Certify barriers in both directions from the restored pose.

    Each witness is a chord of one native convex collision shape crossing a
    static mesh face. A continuously rotating rigid hinge cannot pass that
    angle without penetrating the obstacle. Keep the blocked angles inside
    the envelope rather than estimating the exact contact angle.
    """

    def witness(angle):
        for hull_index, hull in enumerate(hulls):
            points = [rotate(p, pivot, axis, math.radians(angle)) for p in hull]
            box = bounds(points)
            for face in faces:
                tri = face['points']
                other = bounds(tri)
                if any(box[k] > other[k + 1] or box[k + 1] < other[k] for k in (0, 2, 4)):
                    continue
                for a, b in combinations(points, 2):
                    contact = segment_face(a, b, tri)
                    if contact is not None:
                        return {
                            'angle': angle,
                            'hull': hull_index,
                            'surface': face['surface'],
                            'triangle': tri,
                            'chord': [a, b],
                            'contact': contact,
                        }
        return None

    if not hulls or not faces or witness(0):
        return None
    barriers = []
    for sign in (-1, 1):
        found = next((w for angle in range(step, 360, step) if (w := witness(sign * angle))), None)
        if found is None:
            return None
        barriers.append(found)
    low, high = (b['angle'] for b in barriers)
    return {'angles': [low, high], 'barriers': barriers} if high - low < 360 else None
