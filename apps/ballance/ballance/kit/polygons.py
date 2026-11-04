"""Polygon domains with voids, tessellated without filling holes or notches."""

import math

from .shapes import polygon, inside, intersects, cross, inset_outline


def edges(loop):
    return list(zip(loop, loop[1:]+loop[:1]))


def domain(outline, holes):
    """Validate disjoint, strictly interior holes in either winding."""
    if not isinstance(holes, list) or len(holes) > 32:
        raise ValueError('polygon holes must be a list of at most 32 contours')
    holes = [polygon(hole) for hole in holes]
    for i, hole in enumerate(holes):
        if not inside(hole[0], outline) or any(intersects(a,b,c,d) for a,b in edges(hole) for c,d in edges(outline)):
            raise ValueError('polygon hole must be strictly inside the outline')
        for other in holes[:i]:
            if inside(hole[0], other) or inside(other[0], hole) or any(intersects(a,b,c,d) for a,b in edges(hole) for c,d in edges(other)):
                raise ValueError('polygon holes must not touch, overlap or nest')
    return holes


def contains(point, outline, holes):
    return inside(point, outline) and not any(inside(point, h) for h in holes)


def hole_borders(holes, width):
    """Expanded void contours and coplanar brick strips around real voids."""
    expanded, patches = [], []
    for hole in holes:
        outer = inset_outline(hole, -width) if width else hole
        expanded.append(outer)
        if not width:
            continue
        for i, (a,b) in enumerate(edges(hole)):
            c,d = outer[(i+1)%len(hole)],outer[i]
            uv = [(0,0),(0,math.dist(a,b)/5),(width/5,math.dist(a,b)/5),(width/5,0)]
            points = [a,b,c,d]
            for indices in ((0,1,2),(0,2,3)):
                if abs(cross(*(points[k] for k in indices))) > 1e-10:
                    patches.append(([points[k] for k in indices],[uv[k] for k in indices]))
    return expanded, patches


def tessellate(outline, holes):
    """Even/odd vertical slabs, including boundary vertices on shared cuts.

    Every vertex x coordinate is a cut. Between cuts, boundary edges cannot
    cross, so paired intersections bound convex trapezoids. Subdividing
    shared vertical edges identically avoids cracks and T junctions.
    """
    domain(outline, holes)
    all_edges = [edge for loop in [outline,*holes] for edge in edges(loop)]
    cuts = sorted({p[0] for loop in [outline,*holes] for p in loop})
    def height(edge,x):
        a,b = edge
        return a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0])
    def crossings(x):
        values = []
        for a,b in all_edges:
            if min(a[0],b[0])-1e-9 <= x <= max(a[0],b[0])+1e-9:
                values.extend([a[1],b[1]] if abs(a[0]-b[0])<1e-12 else [height((a,b),x)])
        return sorted(set(round(v,12) for v in values))
    levels = {x:crossings(x) for x in cuts}
    triangles = []
    for left,right in zip(cuts,cuts[1:]):
        if right-left < 1e-10:
            continue
        mid = (left+right)/2
        active = sorted((e for e in all_edges if min(e[0][0],e[1][0]) < mid < max(e[0][0],e[1][0])), key=lambda e:height(e,mid))
        if len(active)%2:
            raise ValueError('polygon domain has an unpaired boundary')
        for low,high in zip(active[::2],active[1::2]):
            la,lb,ra,rb = height(low,left),height(high,left),height(low,right),height(high,right)
            boundary = [(left,la),(right,ra)]
            boundary += [(right,y) for y in levels[right] if ra+1e-9<y<rb-1e-9]
            boundary += [(right,rb),(left,lb)]
            boundary += [(left,y) for y in reversed(levels[left]) if la+1e-9<y<lb-1e-9]
            centre = (mid,(height(low,mid)+height(high,mid))/2)
            for a,b in edges(boundary):
                if abs(cross(centre,a,b))>1e-10:
                    triangles.append([centre,a,b])
    return triangles
