"""Deterministic endpoint-driven cubic connections in route-local coordinates.

This is a bounded curve solver, not obstacle routing. Horizontal curvature and grade are
certified over the whole cubic using subdivided Bernstein bounds, independently
of the tessellation step. The existing spatial checks still own collisions.
"""

import math

from ..core.path import mix, number, vector


def direction(value):
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise ValueError('endpoint heading needs a unit [forward, right] direction')
    result = tuple(number(v, 'endpoint heading') for v in value)
    if abs(math.hypot(*result) - 1) > 1e-6:
        raise ValueError('endpoint heading must be a unit direction')
    return result


def resolve(spec, before, layout):
    """Resolve a local pose, placed entry or inward-facing named platform port."""
    if not isinstance(spec, dict):
        raise ValueError('endpoint needs an offset, placement or platform port')
    modes = set(spec) & {'offset', 'placement', 'platform'}
    if len(modes) != 1:
        raise ValueError('endpoint needs exactly one of offset, placement or platform')
    mode = next(iter(modes))
    allowed = {mode, 'slope'} | {'offset': {'heading'}, 'placement': set(), 'platform': {'port'}}[
        mode
    ]
    if set(spec) - allowed:
        raise ValueError(f'{mode} endpoint accepts {sorted(allowed)}')
    slope = number(spec.get('slope', 0), 'endpoint slope')
    if mode == 'offset':
        return vector(spec['offset'], 'endpoint offset'), direction(spec.get('heading')), slope
    if mode == 'placement':
        if not isinstance(spec['placement'], str):
            raise ValueError('endpoint placement needs an entity ID')
        pose = layout.placements.get(spec['placement'])
        if pose is None:
            raise ValueError(f'endpoint placement not found: {spec["placement"]}')
        position, heading = pose['position'], pose['heading']
    else:
        if not isinstance(spec['platform'], str) or not isinstance(spec.get('port'), str):
            raise ValueError('endpoint platform and port need names')
        selected = layout.polygon_nodes.get(spec['platform'], {}).get(spec.get('port'))
        if selected is None or selected['used']:
            raise ValueError('endpoint needs an available named platform port')
        port = selected['port']
        position = port.position
        heading = tuple(-v for v in port.heading)
        if port.slope is not None:
            expected = -port.slope
            if 'slope' in spec and abs(slope - expected) > 1e-6:
                raise ValueError('endpoint slope differs from the named platform port')
            slope = expected
    dx, dz = before.heading
    x, y, z = (value - origin for value, origin in zip(position, before.position))
    return (
        (x * dx + z * dz, x * dz - z * dx, y),
        (
            heading[0] * dx + heading[1] * dz,
            heading[0] * dz - heading[1] * dx,
        ),
        slope,
    )


def _split(points):
    a, b, c, d = points
    ab, bc, cd = mix(a, b, 0.5), mix(b, c, 0.5), mix(c, d, 0.5)
    abc, bcd = mix(ab, bc, 0.5), mix(bc, cd, 0.5)
    middle = mix(abc, bcd, 0.5)
    return (a, ab, abc, middle), (middle, bcd, cd, d)


def _derivatives(points):
    velocity = [tuple(3 * (b[k] - a[k]) for k in range(3)) for a, b in zip(points, points[1:])]
    acceleration = [
        tuple(2 * (b[k] - a[k]) for k in range(3)) for a, b in zip(velocity, velocity[1:])
    ]
    return velocity, acceleration


def _cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def _certify(points, radius, grade, depth=0):
    velocity, acceleration = _derivatives(points)
    middle = tuple((velocity[0][k] + 2 * velocity[1][k] + velocity[2][k]) / 4 for k in range(3))
    speed = math.hypot(*middle[:2])
    if speed < 1e-12:
        return False
    accel = mix(*acceleration, 0.5)
    # Reject an observed violation before attempting conservative bounds.
    if abs(middle[2]) > grade * speed or abs(_cross(middle, accel)) * radius > speed**3:
        return False
    lower_speed = min(sum(v[k] * middle[k] / speed for k in (0, 1)) for v in velocity)
    # Product of degree-2 velocity and degree-1 acceleration in Bernstein form.
    cross_controls = [0.0] * 4
    for i, v in enumerate(velocity):
        for j, a in enumerate(acceleration):
            cross_controls[i + j] += math.comb(2, i) / math.comb(3, i + j) * _cross(v, a)
    if (
        lower_speed > 0
        and max(abs(v[2]) for v in velocity) <= grade * lower_speed
        and max(abs(v) for v in cross_controls) * radius <= lower_speed**3
    ):
        return True
    if depth >= 14:
        return False
    return all(_certify(part, radius, grade, depth + 1) for part in _split(points))


def solve(spec, before, layout):
    end, heading, end_slope = resolve(spec['endpoint'], before, layout)
    if before.slope is None and 'start_slope' not in spec:
        raise ValueError('incoming slope is unknown; declare connector start_slope explicitly')
    start_slope = number(spec.get('start_slope', before.slope or 0), 'connector start slope')
    if before.slope is not None and abs(start_slope - before.slope) > 1e-6:
        raise ValueError('connector start slope differs from the incoming path')
    radius = number(spec.get('min_radius', 8), 'connector minimum radius')
    grade = number(spec.get('max_grade', 1), 'connector maximum grade')
    if radius <= 0 or not 0 < grade <= 4:
        raise ValueError('connector minimum radius must be positive and maximum grade in (0,4]')
    if max(abs(start_slope), abs(end_slope)) > grade:
        raise ValueError('connector endpoint slope exceeds maximum grade')
    span = math.hypot(*end[:2])
    if not math.isfinite(span) or span < 1e-6:
        raise ValueError('connector endpoints need finite horizontal separation')
    if 'handles' in spec:
        handles = spec['handles']
        if not isinstance(handles, list) or len(handles) != 2:
            raise ValueError('connector handles need two positive tangent lengths')
        handles = tuple(number(v, 'connector handle') for v in handles)
        if min(handles) <= 0:
            raise ValueError('connector handles must be positive')
        candidates = [handles]
    else:
        # Keep selection stable. Asymmetric handles cover uneven approaches.
        scales = (1 / 3, 0.2, 0.5, 0.75, 1, 1.5)
        candidates = [(span * scale, span * scale) for scale in scales]
        candidates += [
            (span * a, span * b) for a, b in ((0.2, 0.5), (0.5, 0.2), (0.5, 1), (1, 0.5))
        ]
    for first, last in candidates:
        controls = [
            (0, 0, 0),
            (first, 0, first * start_slope),
            (end[0] - last * heading[0], end[1] - last * heading[1], end[2] - last * end_slope),
            end,
        ]
        if _certify(controls, radius, grade):
            return [{'bezier': [list(p) for p in controls[1:]]}]
    raise ValueError(
        f'no endpoint connector satisfies radius >= {radius:g} and grade <= {grade:g}; '
        'move the destination, adjust tangent lengths or explicitly relax the limits'
    )
