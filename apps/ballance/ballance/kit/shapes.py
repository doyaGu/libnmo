"""Free-form floor cross sections and concave platforms, without a grid."""

from __future__ import annotations

import math

from ..core.obj import Mesh
from ..core.path import number
from .floors import RISE, WALL
from .rails import Ring

V_SECTION = [(-0.5, RISE), (0.0, 0.0), (0.5, RISE)]
FLAT_SECTION = [(-0.5, 0.0), (0.0, 0.0), (0.5, 0.0)]


def section(value):
    if value == "v":
        return list(V_SECTION)
    if value == "flat":
        return list(FLAT_SECTION)
    if not isinstance(value, list) or len(value) < 2:
        raise ValueError("section needs 'v', 'flat', or [width fraction, height] pairs")
    if any(not isinstance(p, (list, tuple)) or len(p) != 2 for p in value):
        raise ValueError("section points need [width fraction, height]")
    points = [(number(x, "section"), number(y, "section")) for x, y in value]
    if points[0][0] != -.5 or points[-1][0] != .5 or any(
            a[0] >= b[0] for a, b in zip(points, points[1:])):
        raise ValueError("section fractions must increase from -0.5 to 0.5")
    return points


def section_points(ring: Ring, width: float, profile):
    right, up = ring.axes()
    return [tuple(v + right[i] * fraction * width + up[i] * height
                  for i, v in enumerate((ring.x, ring.y, ring.z)))
            for fraction, height in profile]


def sweep(mesh: Mesh, x: float, z: float, a: Ring, b: Ring, wa: float, wb: float,
          pa, pb, va: float, vb: float, depth: float = WALL,
          cap_start: bool = False, cap_end: bool = False) -> None:
    """One interval of a floor: top, bottom and side walls. UV length is
    cumulative path distance, so adjacent intervals share texture seams."""
    aa, bb = section_points(a, wa, pa), section_points(b, wb, pb)
    au, bu = a.axes()[1], b.axes()[1]
    low_a = [tuple(p[j] - depth * au[j] for j in range(3)) for p in aa]
    low_b = [tuple(p[j] - depth * bu[j] for j in range(3)) for p in bb]
    top = tuple(au[j] + bu[j] for j in range(3))
    bottom = tuple(-v for v in top)
    profile = pa == pb == V_SECTION
    adapter = pa in (V_SECTION, FLAT_SECTION) and pb in (V_SECTION, FLAT_SECTION) and pa != pb
    material = ("Floor_Top_Profile" if profile else
                "Floor_Top_ProfileFlat" if adapter else "Floor_Top_Boderless")
    banded = (profile or adapter) and max(wa, wb) > 5
    if banded:
        # The native Profile bitmap carries its brick edge. Stretching one
        # copy over a wider cross-section also stretches those bricks. Keep
        # each outer band 2.5 units wide and tile the stone interior instead.
        def bands(ring, width, shape):
            inner = max(0, .5 - 2.5 / width)
            fractions = [-.5, -inner, 0, inner, .5]
            points = section_points(ring, width, [(u, abs(u)*2*RISE if shape == V_SECTION else 0)
                                                   for u in fractions])
            return fractions, points

        fa, ca = bands(a, wa, pa)
        fb, cb = bands(b, wb, pb)
        ta, tb = (1-va, 1-vb) if adapter and pa == FLAT_SECTION else (va, vb)
        for i in range(4):
            edge = i in (0, 3)
            uv_a = [((.5*k if i == 0 else .5+.5*k), ta) if edge else
                    (fa[i+k]*wa/5, va) for k in (0, 1)]
            uv_b = [((.5*k if i == 0 else .5+.5*k), tb) if edge else
                    (fb[i+k]*wb/5, vb) for k in (0, 1)]
            corners = [(ca[i], uv_a[0]), (ca[i+1], uv_a[1]),
                       (cb[i+1], uv_b[1]), (cb[i], uv_b[0])]
            for indices in ((0, 1, 2), (0, 2, 3)):
                triangle = [corners[k] for k in indices]
                p, q, r = [c[0] for c in triangle]
                pq, pr = [q[k]-p[k] for k in range(3)], [r[k]-p[k] for k in range(3)]
                area = sum((pq[(k+1)%3]*pr[(k+2)%3]-pq[(k+2)%3]*pr[(k+1)%3])**2
                           for k in range(3))
                # A widening floor starts with a zero-width interior.
                if area > 1e-16:
                    mesh.triangle(material if edge else "Floor_Top_Boderless",
                                  top, *triangle)
    for i in range(len(pa) - 1):
        u0, u1 = pa[i][0] + .5, pa[i + 1][0] + .5
        if not banded:
            ua = (u0, u1) if profile or adapter else (u0*wa/5, u1*wa/5)
            ub = (u0, u1) if profile or adapter else (u0*wb/5, u1*wb/5)
            v0, v1 = (1-va, 1-vb) if adapter and pa == FLAT_SECTION else (va, vb)
            corners = [(aa[i], (ua[0], v0)), (aa[i + 1], (ua[1], v0)),
                       (bb[i + 1], (ub[1], v1)), (bb[i], (ub[0], v1))]
            # Fan both halves out from the low point of the V. A fixed
            # quad diagonal makes one half flatten early, leaving a kink
            # whose shading disagrees with the native ProfileFlat bitmap.
            flip = adapter and ((pa == V_SECTION and i == 0) or
                                (pb == V_SECTION and i == 1))
            for tri in (((1, 2, 3), (1, 3, 0)) if flip else ((0, 1, 2), (0, 2, 3))):
                mesh.triangle(material, top, *[corners[k] for k in tri])
        mesh.quad("Floor_Side", bottom, (low_a[i], (u0, va)), (low_a[i + 1], (u1, va)),
                  (low_b[i + 1], (u1, vb)), (low_b[i], (u0, vb)))
    for i, sign in ((0, -1), (len(pa) - 1, 1)):
        right = a.axes()[0]
        mesh.quad("Floor_Side", tuple(sign * v for v in right),
                  (aa[i], (0, va)), (bb[i], (0, vb)),
                  (low_b[i], (depth / 5, vb)), (low_a[i], (depth / 5, va)))
    for ring, high, low, profile, cap, sign in (
            (a, aa, low_a, pa, cap_start, -1), (b, bb, low_b, pb, cap_end, 1)):
        if cap:
            for i in range(len(profile) - 1):
                mesh.quad("Floor_Side", (sign * ring.dx, 0, sign * ring.dz),
                          (high[i], (0, 0)), (high[i + 1], (1, 0)),
                          (low[i + 1], (1, depth / 5)), (low[i], (0, depth / 5)))


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def inside(p, points):
    """Point in a simple polygon (boundary included)."""
    result = False
    for a, b in zip(points, points[1:] + points[:1]):
        if abs(cross(a, b, p)) < 1e-8 and all(
                min(a[i], b[i]) - 1e-8 <= p[i] <= max(a[i], b[i]) + 1e-8 for i in (0, 1)):
            return True
        if (a[1] > p[1]) != (b[1] > p[1]):
            if p[0] < a[0] + (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]):
                result = not result
    return result


def intersects(a, b, c, d):
    """Closed segment intersection, including collinear overlaps."""
    ac, ad, ca, cb = cross(a, b, c), cross(a, b, d), cross(c, d, a), cross(c, d, b)
    if ac * ad < -1e-12 and ca * cb < -1e-12:
        return True
    return any(abs(area) < 1e-8 and all(min(u[k], v[k]) - 1e-8 <= p[k] <=
               max(u[k], v[k]) + 1e-8 for k in (0, 1))
               for area, p, u, v in ((ac, c, a, b), (ad, d, a, b), (ca, a, c, d), (cb, b, c, d)))


def polygon(value):
    """Validate a simple concave outline; preserve its edge numbering."""
    if not isinstance(value, list) or not 3 <= len(value) <= 512:
        raise ValueError("polygon outline needs 3 to 512 vertices")
    if any(not isinstance(p, (list, tuple)) or len(p) != 2 for p in value):
        raise ValueError("polygon vertices need [forward, right]")
    points = [(number(x, "polygon"), number(y, "polygon")) for x, y in value]
    edges = list(zip(points, points[1:] + points[:1]))
    for i, (a, b) in enumerate(edges):
        if math.dist(a, b) < 1e-6:
            raise ValueError("polygon has a repeated vertex")
        c = points[(i + 2) % len(points)]
        if abs(cross(a, b, c)) < 1e-8 and sum((b[k] - a[k]) * (c[k] - b[k]) for k in (0, 1)) < 0:
            raise ValueError("polygon has overlapping adjacent edges")
        for j in range(i + 1, len(edges)):
            if j == i + 1 or (i == 0 and j == len(edges) - 1):
                continue
            if intersects(a, b, *edges[j]):
                raise ValueError("polygon outline intersects itself")
    if abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in edges)) < 1e-6:
        raise ValueError("polygon has no area")
    return points


def triangulate(points):
    """Ear clipping; unlike a triangle fan, this preserves concave notches."""
    sign = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))
    remaining = list(range(len(points)))
    if sign < 0:
        remaining.reverse()
    # Preserve original edge indices for ports, but omit redundant vertices
    # from triangles so the last triangle cannot have zero area.
    for i in remaining[:]:
        j = remaining.index(i)
        if abs(cross(points[remaining[j - 1]], points[i],
                     points[remaining[(j + 1) % len(remaining)]])) < 1e-10:
            remaining.remove(i)
    result = []
    while len(remaining) > 3:
        for j, b in enumerate(remaining):
            a, c = remaining[j - 1], remaining[(j + 1) % len(remaining)]
            if cross(points[a], points[b], points[c]) <= 1e-10:
                continue
            if any(all(cross(points[u], points[v], points[k]) >= -1e-10
                       for u, v in ((a, b), (b, c), (c, a)))
                   for k in remaining if k not in (a, b, c)):
                continue
            result.append((a, b, c))
            del remaining[j]
            break
        else:
            raise ValueError("cannot triangulate polygon; remove redundant collinear vertices")
    result.append(tuple(remaining))
    return result


def inset_outline(outline, width):
    """Mitered inner contour for a coplanar material border, in either winding."""
    signed = sum(a[0]*b[1]-b[0]*a[1] for a, b in zip(outline, outline[1:]+outline[:1]))
    sign = 1 if signed > 0 else -1
    inset = []
    for i, p in enumerate(outline):
        a, b = outline[i-1], outline[(i+1) % len(outline)]
        normals = []
        for u, v in ((a, p), (p, b)):
            length = math.dist(u, v)
            normals.append((sign*(u[1]-v[1])/length, sign*(v[0]-u[0])/length))
        n, m = normals
        denominator = 1 + sum(x*y for x, y in zip(n, m))
        if denominator < 1e-8:
            raise ValueError('polygon border has an acute reversal; reduce the corner angle')
        inset.append(tuple(p[k]+width*(n[k]+m[k])/denominator for k in (0, 1)))
    try:
        inset = polygon(inset)
    except ValueError as error:
        raise ValueError('polygon border intersects itself; reduce border width') from error
    if not (all(inside(p, outline) for p in inset) if width >= 0 else
            all(inside(p, inset) for p in outline)):
        raise ValueError('polygon border leaves the outline; reduce border width')
    return inset


def bordered_outline(outline, width, openings):
    """Stone contour and brick triangles, including mitered port corners.

    openings maps an edge to its open interval (fractions of its length).
    A port continues the perimeter out onto its adapter: its two concave
    corners must turn into the mouth, not end at a clipped edge strip.
    """
    if not width:
        return outline, []
    inset = inset_outline(outline, width)
    contour, patches = [], []

    def patch(points, uvs):
        for indices in ((0, 1, 2), (0, 2, 3)) if len(points) == 4 else ((0, 1, 2),):
            if abs(cross(*(points[k] for k in indices))) > 1e-10:
                patches.append(([points[k] for k in indices], [uvs[k] for k in indices]))

    sign = 1 if sum(a[0]*b[1]-b[0]*a[1]
                    for a, b in zip(outline, outline[1:]+outline[:1])) > 0 else -1
    for k, (a, b) in enumerate(zip(outline, outline[1:]+outline[:1])):
        ia, ib = inset[k], inset[(k+1) % len(inset)]
        contour.append(ia)
        length = math.dist(a, b)
        e = tuple((b[j]-a[j])/length for j in (0, 1))
        n = (-sign*e[1], sign*e[0])
        if k not in openings:
            patch([a, b, ib, ia], [(0, 0), (0, length/5), (width/5, length/5), (width/5, 0)])
            continue
        lo, hi = openings[k]
        if (hi-lo)*length <= 2*width:
            raise ValueError('polygon border closes its port; reduce border width')
        left, right = [tuple(a[j]+e[j]*t*length for j in (0, 1)) for t in (lo, hi)]
        ml = tuple(left[j]+e[j]*width for j in (0, 1))
        mr = tuple(right[j]-e[j]*width for j in (0, 1))
        il = tuple(ml[j]+n[j]*width for j in (0, 1))
        ir = tuple(mr[j]+n[j]*width for j in (0, 1))
        contour.extend([il, ml, mr, ir])
        patch([a, left, il, ia], [(0, 0), (0, lo*length/5),
                                 (width/5, lo*length/5), (width/5, 0)])
        patch([right, b, ib, ir], [(0, hi*length/5), (0, length/5),
                                  (width/5, length/5), (width/5, hi*length/5)])
        for outer, mouth, inner in ((left, ml, il), (right, mr, ir)):
            patch([outer, mouth, inner], [(0, 1), (width/5, 1), (width/5, 1+width/5)])
    clean = [p for k, p in enumerate(contour) if math.dist(p, contour[k-1]) > 1e-8]
    return polygon(clean), patches


def platform_triangle(mesh: Mesh, x: float, z: float, vertices, depth: float,
                      material="Floor_Top_Boderless", uvs=None):
    texture = uvs if uvs is not None else [(a/5, c/5) for a, _, c in vertices]
    mesh.triangle(material, (0, 1, 0), *list(zip(vertices, texture)))
    mesh.triangle("Floor_Side", (0, -1, 0),
                  *[((a, b-depth, c), (a/5, c/5)) for a, b, c in vertices])


def platform_wall(mesh: Mesh, x: float, z: float, a, b, outward, depth: float):
    length = math.dist(a, b) / 5
    mesh.quad("Floor_Side", outward, (a, (0, 0)), (b, (0, length)),
              ((b[0], b[1] - depth, b[2]), (depth / 5, length)),
              ((a[0], a[1] - depth, a[2]), (depth / 5, 0)))
