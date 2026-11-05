"""Author-owned floor corridors and descending landing corridors.

Points are ball centres in the owning piece's entry frame: forward, right,
up. Width is the permitted lateral movement of the centre, not floor width.
A landing's final disc reserves a recovery area on a particular floor layer.
"""

import math

from ..core.geometry import at
from ..core.path import number, vector
from ..core.space import Volume, bounds, inflate
from ..core.surface import interpolate
from ..game.measures import BALL_RADIUS

SAMPLE_STEP = 0.5


def compile_passages(values, layout):
    if not isinstance(values, list):
        raise ValueError('passages must be a list')
    result, identifiers = [], set()
    for index, value in enumerate(values):
        if not isinstance(value, dict) or set(value) - {
            'id',
            'owner',
            'kind',
            'points',
            'half_width',
            'landing_radius',
        }:
            raise ValueError(
                'passage accepts id, owner, kind, points, half_width and landing_radius'
            )
        identifier, owner = value.get('id'), value.get('owner')
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise ValueError('passage IDs must be nonempty and unique')
        identifiers.add(identifier)
        if not isinstance(owner, str) or owner not in layout.entry_poses:
            raise ValueError('passage owner must reference a route entry')
        kind = value.get('kind')
        if not isinstance(kind, str) or kind not in {'floor', 'landing'}:
            raise ValueError('passage kind must be floor or landing')
        points = value.get('points')
        if not isinstance(points, list) or not 2 <= len(points) <= 512:
            raise ValueError('passage needs 2..512 ball-centre points')
        local = [vector(p, 'passage point') for p in points]
        width = number(value.get('half_width', 0), 'passage half_width')
        radius = number(value.get('landing_radius', 0), 'passage landing_radius')
        if not 0 <= width <= 20 or not 0 <= radius <= 20:
            raise ValueError('passage widths and landing radius must be within 0..20')
        if kind == 'floor' and 'landing_radius' in value:
            raise ValueError('landing_radius requires a landing passage')
        if kind == 'landing' and (
            local[-1][2] >= local[0][2] or any(b[2] > a[2] for a, b in zip(local, local[1:]))
        ):
            raise ValueError('landing path must descend to a lower layer')
        pose = layout.entry_poses[owner]
        x, y, z = pose['position']
        dx, dz = pose['heading']
        world = []
        for forward, right, up in local:
            px, pz = at((x, z), (dx, dz), forward, right)
            world.append((px, y + up, pz))
        if any(math.dist(a, b) < 1e-6 or math.dist(a, b) > 1000 for a, b in zip(world, world[1:])):
            raise ValueError('passage segments must be nonzero and at most 1000 units long')
        result.append(
            {
                'id': identifier,
                'owner': owner,
                'kind': kind,
                'points': world,
                'half_width': width,
                'landing_radius': radius,
                'source': {'pointer': f'/passages/{index}', 'owner': owner},
            }
        )
    return result


def lanes(passage):
    """Parallel sphere sweeps across the requested corridor, at most 0.5 apart."""
    width = passage['half_width']
    count = max(1, math.ceil(2 * width / SAMPLE_STEP))
    offsets = [0] if width == 0 else [-width + 2 * width * i / count for i in range(count + 1)]
    for a, b in zip(passage['points'], passage['points'][1:]):
        length = math.hypot(b[0] - a[0], b[2] - a[2])
        right = (-(b[2] - a[2]) / length, 0, (b[0] - a[0]) / length) if length else (1, 0, 0)
        for offset in offsets:
            yield (
                tuple(v + offset * d for v, d in zip(a, right)),
                tuple(v + offset * d for v, d in zip(b, right)),
            )


def samples(a, b):
    count = max(1, math.ceil(math.dist(a, b) / SAMPLE_STEP))
    return (interpolate(a, b, i / count) for i in range(count + 1))


def landing_points(passage):
    x, y, z = passage['points'][-1]
    radius = passage['landing_radius']
    yield (x, y, z)
    rings = math.ceil(radius / SAMPLE_STEP)
    for ring in range(1, rings + 1):
        distance = radius * ring / rings
        count = max(8, math.ceil(2 * math.pi * distance / SAMPLE_STEP))
        for i in range(count):
            angle = 2 * math.pi * i / count
            yield (x + distance * math.cos(angle), y, z + distance * math.sin(angle))


def volumes(passages):
    for passage in passages:
        segments = list(lanes(passage))
        if passage['kind'] == 'landing':
            segments.extend((p, p) for p in landing_points(passage))
        for a, b in segments:
            yield Volume(
                passage['owner'],
                'floor passage' if passage['kind'] == 'floor' else 'landing clearance',
                inflate(bounds((a, b)), BALL_RADIUS),
                segment=(a, b),
                radius=BALL_RADIUS,
                source={
                    'passage': passage['id'],
                    'entity_id': passage['owner'],
                    'source': passage['source'],
                },
            )
