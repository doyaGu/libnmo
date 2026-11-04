"""Underslung rail ribs: non-colliding scale and bank cues."""
import math

from .rails import Ring


def bar(mesh, a, b, radius=.16):
    delta = [y - x for x, y in zip(a, b)]
    length = math.sqrt(sum(x * x for x in delta))
    if length < 1e-8:
        return
    direction = [x / length for x in delta]
    reference = (0, 1, 0) if abs(direction[1]) < .9 else (1, 0, 0)

    def cross(a, b):
        return [a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0]]

    u = cross(direction, reference)
    norm = math.sqrt(sum(x * x for x in u))
    u = [x / norm for x in u]
    v = cross(direction, u)
    rings = []
    for point in (a, b):
        rings.append([tuple(point[j] + radius * (u[j] * math.cos(k*math.pi/4) +
                                                v[j] * math.sin(k*math.pi/4))
                            for j in range(3)) for k in range(8)])
    for k in range(8):
        n = (k + 1) % 8
        facing = tuple(rings[0][k][j] - a[j] for j in range(3))
        mesh.quad('Rail', facing, (rings[0][k], (0, 0)), (rings[0][n], (1, 0)),
                  (rings[1][n], (1, length/5)), (rings[1][k], (0, length/5)))


def ribs(mesh, rings, spacing, depth, keel=False):
    """Place each rib at its exact arc distance; optionally connect a keel."""
    distance, next_at = 0.0, spacing / 2
    previous = None
    for a, b in zip(rings, rings[1:]):
        length = math.dist((a.x, a.y, a.z), (b.x, b.y, b.z))
        while next_at <= distance + length:
            t = (next_at - distance) / length
            values = {key: getattr(a, key) + (getattr(b, key) - getattr(a, key)) * t
                      for key in ('x', 'y', 'z', 'dx', 'dz', 'bank', 'gauge', 'radius')}
            norm = math.hypot(values['dx'], values['dz'])
            values['dx'] /= norm
            values['dz'] /= norm
            ring = Ring(**values)
            right, up = ring.axes()

            def point(side, down):
                return tuple(p + r*side - u*down
                             for p, r, u in zip((ring.x, ring.y, ring.z), right, up))

            left, right_end = point(-ring.gauge/2, .45), point(ring.gauge/2, .45)
            bottom = point(0, depth)
            for p, q in ((left, right_end), (left, bottom), (bottom, right_end)):
                bar(mesh, p, q)
            if keel and previous is not None:
                bar(mesh, previous, bottom, .22)
            previous = bottom
            next_at += spacing
        distance += length
