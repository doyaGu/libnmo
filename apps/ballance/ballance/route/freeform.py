"""Route adapters for free-form paths and polygon platforms.

Free ports retain their heading, section and bank. Matching free pieces
can be chained off grid; legacy pieces require a cardinal standard port.
"""

from __future__ import annotations

import math
import re
from dataclasses import replace

from ..core.geometry import at
from ..core.path import sample_path, stations, interpolate, number, include_stations
from ..game.measures import ELEMENT_LIFT, BOX_PILE, BEAM_BELOW, BEAM_JOIN
from ..kit.floors import NARROW, RISE, WALL
from ..kit.rails import Ring, Beam, ABOVE_V, GAUGE, TUBE
from ..kit.shapes import (
    section,
    section_points,
    sweep,
    polygon,
    triangulate,
    inside,
    cross,
    platform_triangle,
    platform_wall,
    bordered_outline,
)
from ..kit.polygons import domain, contains, hole_borders, tessellate, edges
from ..kit.shapes import intersects
from . import ports, connectors
from .ports import Section, cardinal

KINDS = ('rail_path', 'beam_path', 'floor_path', 'floor_polygon', 'connector')


def prepare_grid(layout, part, track, piece=None):
    """Validate a free port before allowing a grid-based piece to consume it."""
    if track.free_port is None:
        return
    before = track.free_port
    d = cardinal(track.d)
    # Native modules keep their measured internal adapters. Their incoming
    # interface consumes a standard section at the route datum.
    rail_entry = piece is not None and any(
        key in piece
        for key in (
            'rail',
            'rail_turn',
            'beam',
            'beam_turn',
            'beam_path',
            'fan',
            'tower',
            'cradle',
            'fall',
        )
    )
    after = ports.entry(
        piece,
        track,
        Section('rail' if rail_entry else 'floor'),
        owner=None if piece is not None else f'sector/{len(layout.sectors)}/end',
    )
    connection = ports.connect(before, after, grid=True)
    if piece and 'area' in piece and before.section.kind == 'floor':
        raise ValueError('connect a free floor to an area through a narrow road')
    layout.connections.append(connection)
    track.d = d
    # A helix can return to the same XZ position on a different level.
    # A legacy grid cannot store both heights at the same vertices.
    layout.floors = part.grid(track.p)
    if before.section.kind == 'floor':
        layout.open(layout.floors, track.p, track.d)
    track.free_port = None


def _open_entry(layout, part, track, kind, section, piece):
    before = ports.incoming(layout, track)
    after = ports.entry(piece, track, section)
    if before.section.kind == kind == 'floor':
        after = replace(after, distance=before.distance)
    connection = ports.connect(before, after)
    if track.railed or track.floor in ('fall', 'node', 'log', 'trough'):
        raise ValueError('a free piece connects to a road or another path')
    if isinstance(track.run, Beam):
        raise ValueError('connect a beam to a free path through a standard rail')
    if track.free_port is None and track.run is None:
        if track.floor not in ('road', 'turn', 'module', 'trafo') or track.width != NARROW:
            raise ValueError('a free piece leaves a narrow road or a matching free port')
        if kind == 'rail':
            layout.dock(layout.floors, track.p, track.d)
        else:
            layout.open(layout.floors, track.p, track.d)
    if kind == 'floor' and track.run is not None:
        part.rails.runs.append(track.run)
        track.run = None
    layout.connections.append(connection)
    return after


def _world(point, p, d, h):
    x, z = at(p, d, point[0], point[1])
    return x, h + point[2], z


def _direction(local, d):
    return d[0] * local[0] + d[1] * local[1], d[1] * local[0] - d[0] * local[1]


def _profile(a, b, t):
    return [(x, y + (v - y) * t) for (x, y), (_, v) in zip(a, b)]


def lay_path(layout, index, part, piece, track):
    beam = 'beam_path' in piece
    rail = 'rail_path' in piece or beam
    kind = 'rail' if rail else 'floor'
    spec = piece['beam_path' if beam else 'rail_path' if rail else 'floor_path']
    samples = sample_path(spec)
    allowed = {'segments', 'step', 'tolerance', 'bank', 'stops'} | (
        {'radius'}
        if beam
        else {'gauge', 'radius', 'ribs'}
        if rail
        else {'width', 'section', 'end_section', 'depth'}
    )
    if set(spec) - allowed:
        raise ValueError(f'unknown {kind} path options: {", ".join(sorted(set(spec) - allowed))}')
    banks = stations(spec.get('bank', 0), 'bank', -75, 75)
    if rail:
        widths = stations(spec.get('gauge', GAUGE), 'gauge', 0.2, 20)
        radii = stations(spec.get('radius', TUBE), 'radius', 0.05, 3)
    else:
        widths = stations(spec.get('width', NARROW), 'width', 0.5, 100)
        first = section(spec.get('section', 'v'))
        last = section(spec.get('end_section', spec.get('section', 'v')))
        if [p[0] for p in first] != [p[0] for p in last]:
            raise ValueError('start and end sections must have identical width fractions')
        depth = number(spec.get('depth', WALL), 'floor depth')
        if not 0.1 <= depth <= 100:
            raise ValueError('floor depth must be within 0.1..100')
    stops = {}
    for stop in spec.get('stops', []):
        if not isinstance(stop, list) or len(stop) != 2:
            raise ValueError('path stops need [distance fraction, frames]')
        fraction, frames = number(stop[0], 'stop fraction'), stop[1]
        if (
            not 0 < fraction <= 1
            or not isinstance(frames, int)
            or isinstance(frames, bool)
            or not 1 <= frames <= 10000
        ):
            raise ValueError('stop fraction must be in (0,1] and frames an integer in 1..10000')
        if fraction in stops:
            raise ValueError('duplicate path stop fraction')
        stops[fraction] = frames
    samples = include_stations(
        samples,
        [t for keys in (banks, widths, radii if rail else []) for t, _ in keys] + list(stops),
    )

    def port(t):
        if rail:
            return Section(
                kind,
                bank=interpolate(banks, t),
                gauge=interpolate(widths, t),
                radius=interpolate(radii, t),
            )
        return Section(
            kind,
            bank=interpolate(banks, t),
            width=interpolate(widths, t),
            profile=tuple(_profile(first, last, t)),
            depth=depth,
        )

    uv_start = 0
    if beam:
        # Use the same overlapped height transition as the original single
        # beams. Cardinal end ports keep beam_turn and rail exits unambiguous.
        from .pieces import height_back

        prepare_grid(layout, part, track, piece)
        if track.run is None or track.railed:
            raise ValueError('beam_path starts from an unguarded rail or beam')
        if (
            cardinal(track.d) is None
            or cardinal(_direction(samples[-1].direction, track.d)) is None
        ):
            raise ValueError('beam_path requires cardinal entry and exit headings')
        if abs(radii[0][1] - TUBE) > 1e-6 or abs(radii[-1][1] - TUBE) > 1e-6:
            raise ValueError('beam_path end radii must match the standard beam')
        if not isinstance(track.run, Beam):
            part.rails.runs.append(track.run)
            back = at(track.p, track.d, -BEAM_JOIN)
            track.h -= BEAM_BELOW
            track.run = Beam(
                [
                    Ring(
                        back[0],
                        height_back(track.run, BEAM_JOIN) - BEAM_BELOW,
                        back[1],
                        *track.d,
                        gauge=0,
                    ),
                    Ring(track.p[0], track.h + ABOVE_V, track.p[1], *track.d, gauge=0),
                ]
            )
    else:
        entry = _open_entry(layout, part, track, kind, port(0), piece)
        uv_start = entry.distance
    origin, direction, height = track.p, track.d, track.h
    rings, profiles, sizes = [], [], []
    for sample in samples:
        t = sample.distance / samples[-1].distance
        x, y, z = _world(sample.point, origin, direction, height + (ABOVE_V if rail else 0))
        dx, dz = _direction(sample.direction, direction)
        width, bank = interpolate(widths, t), interpolate(banks, t)
        radius = interpolate(radii, t) if rail else TUBE
        if rail and not beam and width <= 2 * radius:
            raise ValueError('rail tubes overlap: gauge must exceed twice the radius')
        rings.append(Ring(x, y, z, dx, dz, bank, 0 if beam else width if rail else GAUGE, radius))
        sizes.append(width)
        if not rail:
            profiles.append(_profile(first, last, t))
    if rail:
        if 'ribs' in spec:
            from ..kit.architecture import ribs

            settings = spec['ribs']
            if not isinstance(settings, dict) or set(settings) - {'spacing', 'depth', 'keel'}:
                raise ValueError('ribs accept spacing, depth and keel')
            spacing = number(settings.get('spacing', 24), 'rib spacing')
            rib_depth = number(settings.get('depth', 3), 'rib depth')
            keel = settings.get('keel', False)
            if not isinstance(keel, bool):
                raise ValueError('rib keel must be boolean')
            if not 8 <= spacing <= 100 or not 1 <= rib_depth <= 8:
                raise ValueError('rib spacing must be 8..100 and depth 1..8')
            ribs(part.decoration, rings, spacing, rib_depth, keel)
        for a, b in zip(rings, rings[1:]):
            delta = (b.x - a.x, b.z - a.z)
            for sign in (-1, 1):
                ends = [
                    (
                        r.x + sign * r.axes()[0][0] * (r.gauge / 2 + r.radius),
                        r.z + sign * r.axes()[0][2] * (r.gauge / 2 + r.radius),
                    )
                    for r in (a, b)
                ]
                if sum((ends[1][k] - ends[0][k]) * delta[k] for k in (0, 1)) <= 1e-9:
                    raise ValueError(
                        'rail folds inside a bend; reduce gauge or increase the turn radius'
                    )
        if track.run is None:
            track.run = rings
        else:
            track.run.extend(rings[1:])
        track.floor = 'beam' if beam else 'rail'
    else:
        for i, (a, b) in enumerate(zip(rings, rings[1:])):
            aa, bb = (
                section_points(a, sizes[i], profiles[i]),
                section_points(b, sizes[i + 1], profiles[i + 1]),
            )
            delta = (b.x - a.x, b.z - a.z)
            if any(
                (q[0] - p[0]) * delta[0] + (q[2] - p[2]) * delta[1] <= 1e-9 for p, q in zip(aa, bb)
            ):
                raise ValueError(
                    'floor folds inside a bend; reduce width or increase the turn radius'
                )
            part.extras.append(
                (
                    sweep,
                    (0, 0),
                    a,
                    b,
                    sizes[i],
                    sizes[i + 1],
                    profiles[i],
                    profiles[i + 1],
                    (uv_start + samples[i].distance) / 5,
                    (uv_start + samples[i + 1].distance) / 5,
                    depth,
                )
            )
        track.width, track.floor = sizes[-1], 'road'
    # Keep steering points far enough apart to avoid the controller skipping
    # an entire small bend, while retaining a maximum 10-degree heading change.
    previous = samples[0]
    for sample, ring in zip(samples[1:], rings[1:]):
        dot = sum(a * b for a, b in zip(previous.direction, sample.direction))
        hold = next(
            (
                frames
                for t, frames in stops.items()
                if abs(sample.distance / samples[-1].distance - t) < 1e-10
            ),
            0,
        )
        if (
            hold
            or sample.distance - previous.distance >= 3
            or dot < math.cos(math.radians(10))
            or sample is samples[-1]
        ):
            layout.points.append(
                [ring.x, ring.z, None, hold]
                if hold
                else [ring.x, ring.z, None, 'precision']
                if beam
                else [ring.x, ring.z]
            )
            previous = sample
    track.p, track.d = (rings[-1].x, rings[-1].z), (rings[-1].dx, rings[-1].dz)
    track.h = height + samples[-1].point[2]
    track.free_port = (
        None
        if beam
        else replace(
            entry,
            name='exit',
            position=(track.p[0], track.h, track.p[1]),
            heading=track.d,
            section=port(1),
            distance=uv_start + samples[-1].distance if kind == 'floor' else 0,
        )
    )
    if beam:
        track.d = cardinal(track.d)
    for element in piece.get('elements', []):
        along, aside = element['at']
        along, aside = number(along, 'element distance'), number(aside, 'element offset')
        if not 0 <= along <= samples[-1].distance:
            raise ValueError('element distance must lie on its path')
        k = next(i for i in range(1, len(samples)) if samples[i].distance >= along)
        f = (along - samples[k - 1].distance) / (samples[k].distance - samples[k - 1].distance)
        a, b = rings[k - 1], rings[k]
        dx, dz = a.dx + (b.dx - a.dx) * f, a.dz + (b.dz - a.dz) * f
        norm = math.hypot(dx, dz)
        dx, dz = dx / norm, dz / norm
        t = along / samples[-1].distance
        width = 2 * interpolate(radii, t) if beam else interpolate(widths, t)
        if abs(aside) > width / 2:
            raise ValueError('element offset is outside its path')
        profile_height = 0
        if not rail:
            profile = _profile(first, last, t)
            fraction = aside / width
            for (u, y), (v, z) in zip(profile, profile[1:]):
                if fraction <= v:
                    profile_height = y + (z - y) * (fraction - u) / (v - u)
                    break
        bank = math.radians(interpolate(banks, t))
        offset = aside * math.cos(bank) - profile_height * math.sin(bank)
        where = [
            a.x + (b.x - a.x) * f + dz * offset,
            a.y
            + (b.y - a.y) * f
            + aside * math.sin(bank)
            + profile_height * math.cos(bank)
            - (ABOVE_V if rail else 0),
            a.z + (b.z - a.z) * f - dx * offset,
        ]
        _element(layout, index, element, where)


def _element(layout, index, element, where):
    kind = element['kind']
    if kind not in ELEMENT_LIFT or kind == 'P_Modul_18':
        raise ValueError('free surfaces accept rewards, boxes and loose balls; fans need a housing')
    pile = element.get('pile', 1)
    if not isinstance(pile, int) or not 1 <= pile <= 100 or (pile != 1 and kind != 'P_Box'):
        raise ValueError('only boxes may be piled (1..100)')
    for k in range(pile):
        layout.elements.append(
            (
                kind,
                index,
                [
                    where[0],
                    where[1]
                    + ELEMENT_LIFT[kind]
                    + number(element.get('up', 0), 'element up')
                    + k * BOX_PILE,
                    where[2],
                ],
            )
        )


def _segment_inside(a, b, outline):
    # Test every interval between crossings of an outline edge's infinite
    # line; this also catches notches much narrower than a sampling step.
    ts = [0.0, 1.0]
    for c, d in zip(outline, outline[1:] + outline[:1]):
        ca, cb = cross(c, d, a), cross(c, d, b)
        if abs(ca - cb) > 1e-10:
            t = ca / (ca - cb)
            if 0 < t < 1:
                ts.append(t)
    ts.sort()
    return all(
        inside((a[0] + (b[0] - a[0]) * (u + v) / 2, a[1] + (b[1] - a[1]) * (u + v) / 2), outline)
        for u, v in zip(ts, ts[1:])
    )


def lay_polygon(layout, index, part, piece, track):
    spec = piece['floor_polygon']
    if not isinstance(spec, dict) or set(spec) - {
        'outline',
        'holes',
        'ports',
        'supports',
        'entry',
        'exit',
        'path',
        'depth',
        'border',
    }:
        raise ValueError(
            'floor_polygon accepts outline, holes, ports, supports, entry, '
            'exit, path, depth and border'
        )
    outline = polygon(spec.get('outline'))
    holes = domain(outline, spec.get('holes', []))
    border = number(spec.get('border', 0.75), 'polygon border')
    if not 0 <= border <= 2.5:
        raise ValueError('polygon border must be within 0..2.5')
    depth = number(spec.get('depth', WALL), 'polygon depth')
    if not 0.1 <= depth <= 100:
        raise ValueError('polygon depth must be within 0.1..100')
    signed = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(outline, outline[1:] + outline[:1]))

    def port(value, paths=False):
        if not isinstance(value, dict) or set(value) - (
            {'edge', 'at', 'width'} | ({'path'} if paths else set())
        ):
            raise ValueError('polygon ports need an edge index and optional at fraction and width')
        edge = value.get('edge')
        t = number(value.get('at', 0.5), 'port fraction')
        if not isinstance(edge, int) or isinstance(edge, bool) or not 0 <= edge < len(outline):
            raise ValueError('polygon port edge is out of range')
        a, b = outline[edge], outline[(edge + 1) % len(outline)]
        length = math.dist(a, b)
        width = number(value.get('width', NARROW), 'polygon port width')
        if not 0.5 <= width <= 100:
            raise ValueError('polygon port width must be within 0.5..100')
        if not width / 2 <= length * t <= length - width / 2:
            raise ValueError('polygon port must fit its edge')
        mid = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        sign = 1 if signed > 0 else -1
        out = (sign * (b[1] - a[1]) / length, -sign * (b[0] - a[0]) / length)
        return edge, t, mid, out, length, width

    entry, leave = port(spec.get('entry')), port(spec.get('exit'))
    if entry[0] == leave[0]:
        raise ValueError('polygon entry and exit must use different edges')
    if math.dist(entry[2], (0, 0)) > 1e-6 or math.dist(entry[3], (-1, 0)) > 1e-6:
        raise ValueError('polygon entry must be centred at [0,0], facing backward')
    extra = spec.get('ports', {})
    if not isinstance(extra, dict) or any(
        not isinstance(k, str)
        or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*', k)
        or k in ('entry', 'exit')
        for k in extra
    ):
        raise ValueError('polygon ports need distinct names other than entry and exit')
    named = {name: port(value, paths=True) for name, value in extra.items()}
    all_ports = [entry, leave, *named.values()]
    if len({value[0] for value in all_ports}) != len(all_ports):
        raise ValueError('polygon ports currently require distinct outline edges')
    openings = {
        edge: (t - width / 2 / length, t + width / 2 / length)
        for edge, t, _, _, length, width in all_ports
    }
    inner, border_patches = bordered_outline(outline, border, openings)
    expanded, hole_patches = hole_borders(holes, border)
    domain(inner, expanded)
    top_triangles = (
        tessellate(inner, expanded)
        if holes
        else [[inner[k] for k in tri] for tri in triangulate(inner)]
    )
    border_patches += hole_patches

    def path(waypoints, destination):
        route = [(0.0, 0.0)]
        if not isinstance(waypoints, list):
            raise ValueError('polygon path must be a list of waypoints')
        for point in waypoints:
            if not isinstance(point, list) or len(point) != 2:
                raise ValueError('polygon path points need [forward, right]')
            route.append(tuple(number(v, 'polygon path') for v in point))
        route.append(destination)
        if not all(contains(p, outline, holes) for p in route) or not all(
            _segment_inside(a, b, outline)
            and not any(intersects(a, b, c, d) for hole in holes for c, d in edges(hole))
            for a, b in zip(route, route[1:])
        ):
            raise ValueError(
                'polygon steering path leaves the surface or crosses a hole; '
                'supply interior waypoints'
            )
        return route

    route = path(spec.get('path', []), leave[2])
    approaches = {
        name: path(extra[name].get('path', []), value[2]) for name, value in named.items()
    }
    # Reserve 5 units before the platform for a V-to-flat adapter.
    p, d, h = track.p, track.d, track.h
    origin = at(p, d, 5)
    base = h + RISE
    inlet = _open_entry(
        layout, part, track, 'floor', Section('floor', width=entry[5], depth=depth), piece
    )

    def world(v):
        x, z = at(origin, d, *v)
        return x, base, z

    for tri in top_triangles:
        part.extras.append((platform_triangle, (0, 0), [world(v) for v in tri], depth))
    for vertices, uvs in border_patches:
        part.extras.append(
            (
                platform_triangle,
                (0, 0),
                [world(v) for v in vertices],
                depth,
                'Floor_Top_Border',
                uvs,
            )
        )
    for k, (a, b) in enumerate(zip(outline, outline[1:] + outline[:1])):
        intervals = [(0, 1)]
        for edge, t, _, _, length, width in all_ports:
            if edge == k:
                intervals = [(0, t - width / 2 / length), (t + width / 2 / length, 1)]
        sign = 1 if signed > 0 else -1
        out = _direction((sign * (b[1] - a[1]), -sign * (b[0] - a[0])), d)
        for u, v in intervals:
            if v - u > 1e-8:
                aa = (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)
                bb = (a[0] + (b[0] - a[0]) * v, a[1] + (b[1] - a[1]) * v)
                part.extras.append(
                    (platform_wall, (0, 0), world(aa), world(bb), (out[0], 0, out[1]), depth)
                )
    for hole in holes:
        winding = 1 if sum(a[0] * b[1] - b[0] * a[1] for a, b in edges(hole)) > 0 else -1
        for a, b in edges(hole):
            normal = _direction((-winding * (b[1] - a[1]), winding * (b[0] - a[0])), d)
            part.extras.append(
                (platform_wall, (0, 0), world(a), world(b), (normal[0], 0, normal[1]), depth)
            )
    ex, _, ez = world(leave[2])
    ed = _direction(leave[3], d)
    end = at((ex, ez), ed, 5)
    part.extras.extend(connectors.platform_mouth(p, d, h, entry[5], depth, distance=inlet.distance))
    part.extras.extend(connectors.platform_mouth((ex, ez), ed, h, leave[5], depth, leaving=True))
    main_path = [[world(v)[0], world(v)[2]] for v in route] + [list(end)]
    branch_ports = {}
    for name, value in named.items():
        bx, _, bz = world(value[2])
        direction = _direction(value[3], d)
        tip = at((bx, bz), direction, 5)
        attachment = replace(
            inlet,
            name=name,
            position=(tip[0], h, tip[1]),
            heading=direction,
            section=Section('floor', width=value[5], depth=depth),
            distance=2.5,
        )
        approach = [list(p)] + [[world(v)[0], world(v)[2]] for v in approaches[name]] + [list(tip)]
        branch_ports[name] = {
            'port': attachment,
            'approach': approach,
            'used': False,
            'sector': index,
        }
        part.extras.extend(
            connectors.platform_mouth((bx, bz), direction, h, value[5], depth, leaving=True)
        )
    for branch in piece.get('branches', []):
        name = branch.get('port')
        if name not in branch_ports or branch_ports[name]['used'] or 'side' in branch:
            raise ValueError('polygon branch must select one unused named port')
        selected = branch_ports[name]
        selected['used'] = True
        attachment = selected['port']
        tip = (attachment.position[0], attachment.position[2])
        grid = part.grid(tip)
        track.forks.append(
            (
                tip,
                attachment.heading,
                h,
                grid,
                branch,
                'road',
                {**selected, 'main_tail': len(main_path)},
            )
        )
    if any(not value['used'] for value in branch_ports.values()) and 'name' not in piece:
        raise ValueError('unused polygon ports need a named platform and incoming branches')
    if 'name' in piece:
        layout.name_node(piece['name'], layout.floors, h, [])
        layout.polygon_nodes[piece['name']] = branch_ports
    layout.platforms.append(
        {
            'entity': inlet.owner,
            'sector': index,
            'source': inlet.source.data(),
            'holes': [[world(v) for v in hole] for hole in holes],
            'ports': [inlet.data(), *[value['port'].data() for value in branch_ports.values()]],
            'depth': depth,
        }
    )
    layout.points += main_path
    track.p, track.d, track.h = end, ed, h
    track.width, track.floor = leave[5], 'road'
    track.free_port = replace(
        inlet,
        name='exit',
        position=(end[0], h, end[1]),
        heading=ed,
        section=Section('floor', width=leave[5], depth=depth),
        distance=2.5,
    )
    layout.platforms[-1]['ports'].append(track.free_port.data())
    supports = spec.get('supports', [])
    if not isinstance(supports, list):
        raise ValueError('polygon supports need [forward, right] points')
    for coords in supports:
        if not isinstance(coords, list) or len(coords) != 2:
            raise ValueError('polygon supports need [forward, right] points')
        coords = [number(v, 'polygon support') for v in coords]
        if not contains(coords, outline, holes):
            raise ValueError('polygon support lies outside its surface or inside a hole')
        sx, sy, sz = world(coords)
        layout.pillars.append([sx, sy - depth, sz])
    for element in piece.get('elements', []):
        coords = element['at']
        if len(coords) != 2 or not contains(coords, outline, holes):
            raise ValueError('polygon element lies outside its surface or inside a hole')
        where = list(world(coords))
        _element(layout, index, element, where)


def lay(layout, index, part, piece, track):
    if 'connector' in piece:
        before = ports.incoming(layout, track)
        key, path = connectors.transition(piece['connector'], before.section)
        generated = piece.copy()
        del generated['connector']
        generated[key] = path
        # Keep the authored connector's source identity in both port records.
        from .identity import Piece

        generated = Piece(generated, piece.origin)
        lay_path(layout, index, part, generated, track)
        layout.transitions.append(
            {
                'entity': piece.origin.data(),
                'from': before.data(),
                'to': track.free_port.data(),
                'path': path,
            }
        )
    elif 'floor_polygon' in piece:
        lay_polygon(layout, index, part, piece, track)
    else:
        lay_path(layout, index, part, piece, track)
