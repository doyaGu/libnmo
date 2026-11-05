"""Sample authored 3D paths in local (forward, right, up) coordinates.

The path owns sampling and tangents; rail and floor builders consume the
same samples. Cross sections stay upright on slopes and may bank about
their horizontal heading, matching Ballance's existing rail construction.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

Vec = tuple[float, float, float]


def number(value, name: str) -> float:
    if isinstance(value, bool):
        raise ValueError(f'{name} must be a finite number')
    try:
        result = float(value)
    except (ValueError, TypeError) as error:
        raise ValueError(f'{name} must be a finite number') from error
    if not math.isfinite(result):
        raise ValueError(f'{name} must be a finite number')
    return result


def vector(value, name: str = 'path point') -> Vec:
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError(f'{name} needs [forward, right, up]')
    return tuple(number(v, name) for v in value)


def mix(a: Vec, b: Vec, t: float) -> Vec:
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def heading(v: Vec) -> tuple[float, float]:
    length = math.hypot(v[0], v[1])
    if length < 1e-8:
        raise ValueError('a path cannot have a vertical or zero horizontal tangent')
    return v[0] / length, v[1] / length


@dataclass(frozen=True)
class Sample:
    point: Vec
    direction: tuple[float, float]
    distance: float
    slope: float | None = None


def sample_path(spec: dict) -> list[Sample]:
    """Lines end at absolute local points; cubics specify two controls and
    an end. Arcs turn right by angle degrees from the current tangent,
    around radius, and gain rise units. Positive multi-turn arcs are helices.

    Cubics are subdivided by control-polygon flatness and length, so an
    S-curve with a midpoint on its chord is still sampled correctly.
    """
    if not isinstance(spec, dict):
        raise ValueError('a path must be an object with segments')
    segments = spec.get('segments')
    if not isinstance(segments, list) or not segments or len(segments) > 512:
        raise ValueError('a path needs 1 to 512 segments')
    step = number(spec.get('step', 2.0), 'path step')
    tolerance = number(spec.get('tolerance', 0.025), 'path tolerance')
    if not 0.1 <= step <= 10 or not 0.001 <= tolerance <= 0.5:
        raise ValueError('path step must be 0.1..10 and tolerance 0.001..0.5')
    raw: list[tuple[Vec, tuple[float, float], float | None]] = []
    current: Vec = (0.0, 0.0, 0.0)
    direction = (1.0, 0.0)

    def append(p, d, slope=None):
        if len(raw) >= 20000:
            raise ValueError('path exceeds 20000 samples; use a coarser step')
        raw.append((p, d, slope))

    def cubic(points, depth=0):
        a, b, c, d = points
        chord = math.dist(a, d)
        polygon = math.dist(a, b) + math.dist(b, c) + math.dist(c, d)

        def off_chord(p):
            t = (
                sum((p[i] - a[i]) * (d[i] - a[i]) for i in range(3)) / chord**2
                if chord > 1e-12
                else 0.0
            )
            return math.dist(p, mix(a, d, min(1.0, max(0.0, t))))

        if polygon <= step and max(off_chord(b), off_chord(c)) <= tolerance:
            delta = tuple(d[i] - c[i] for i in range(3))
            append(d, heading(delta), delta[2] / math.hypot(*delta[:2]))
            return
        if depth >= 20:
            raise ValueError('Bezier cannot be sampled: degenerate controls or excessive scale')
        ab, bc, cd = mix(a, b, 0.5), mix(b, c, 0.5), mix(c, d, 0.5)
        abc, bcd = mix(ab, bc, 0.5), mix(bc, cd, 0.5)
        mid = mix(abc, bcd, 0.5)
        cubic((a, ab, abc, mid), depth + 1)
        cubic((mid, bcd, cd, d), depth + 1)

    for segment in segments:
        if not isinstance(segment, dict) or len(segment) != 1:
            raise ValueError('each path segment must contain exactly one of line, bezier, arc')
        kind, value = next(iter(segment.items()))
        initial_slope = None
        if kind == 'line':
            end = vector(value)
            delta = tuple(end[i] - current[i] for i in range(3))
            initial = final = heading(delta)
            initial_slope = delta[2] / math.hypot(*delta[:2])
            count = math.ceil(math.dist(current, end) / step)
        elif kind == 'bezier':
            if not isinstance(value, list) or len(value) != 3:
                raise ValueError('bezier needs two control points and an endpoint')
            b, c, end = [vector(v) for v in value]
            initial = heading(tuple(b[i] - current[i] for i in range(3)))
            initial_slope = (b[2] - current[2]) / math.hypot(b[0] - current[0], b[1] - current[1])
            final = heading(tuple(end[i] - c[i] for i in range(3)))
        elif kind == 'arc':
            if not isinstance(value, dict) or set(value) - {'radius', 'angle', 'rise'}:
                raise ValueError('arc needs radius, angle and optional rise')
            radius = number(value.get('radius'), 'arc radius')
            angle = math.radians(number(value.get('angle'), 'arc angle'))
            rise = number(value.get('rise', 0), 'arc rise')
            if radius <= 0 or not 0 < abs(angle) <= 8 * math.pi:
                raise ValueError('arc radius must be positive and angle within +/-1440 degrees')
            initial = direction
            initial_slope = rise / (radius * abs(angle))
            sign = 1 if angle > 0 else -1
            # Both coordinate axes are local: right of (forward, right).
            right = (-direction[1], direction[0])
            centre = (current[0] + sign * radius * right[0], current[1] + sign * radius * right[1])
            limit = 2 * math.acos(max(-1, 1 - tolerance / radius))
            count = max(
                1,
                math.ceil(math.hypot(radius * angle, rise) / step),
                math.ceil(abs(angle) / max(limit, 1e-6)),
            )

            def arc_point(t):
                a = angle * t
                p = (
                    centre[0]
                    + sign * radius * (math.sin(a) * direction[0] - math.cos(a) * right[0]),
                    centre[1]
                    + sign * radius * (math.sin(a) * direction[1] - math.cos(a) * right[1]),
                    current[2] + rise * t,
                )
                d = (
                    math.cos(a) * direction[0] + math.sin(a) * right[0],
                    math.cos(a) * direction[1] + math.sin(a) * right[1],
                )
                return p, d

            end, final = arc_point(1)
        elif kind in ('wave', 'weave', 'eased_turn'):
            from .formula import sampled

            formula = sampled(kind, value, step, tolerance)
            initial = direction
            right = (-direction[1], direction[0])
            transformed = [
                (
                    (
                        current[0] + p[0] * direction[0] + p[1] * right[0],
                        current[1] + p[0] * direction[1] + p[1] * right[1],
                        current[2] + p[2],
                    ),
                    (d[0] * direction[0] + d[1] * right[0], d[0] * direction[1] + d[1] * right[1]),
                )
                for p, d in formula
            ]
            end, final = transformed[-1]
        else:
            raise ValueError(f'unknown path segment {kind}')
        if initial[0] * direction[0] + initial[1] * direction[1] < 1 - 1e-5:
            raise ValueError('path segments must meet tangentially; use a Bezier or arc to turn')
        if not raw:
            append(current, initial, initial_slope)
        if kind == 'line':
            if count > 20000:
                raise ValueError('path exceeds 20000 samples')
            for k in range(1, count + 1):
                append(mix(current, end, k / count), initial, initial_slope)
        elif kind == 'bezier':
            cubic((current, b, c, end))
        elif kind in ('wave', 'weave', 'eased_turn'):
            for p, d in transformed[1:]:
                append(p, d)
        else:
            if count > 20000:
                raise ValueError('path exceeds 20000 samples')
            for k in range(1, count + 1):
                append(*arc_point(k / count), initial_slope)
        current, direction = end, final
    result, distance = [], 0.0
    for i, (point, direction, slope) in enumerate(raw):
        if i:
            distance += math.dist(point, raw[i - 1][0])
        result.append(Sample(point, direction, distance, slope))
    if distance < 1e-6:
        raise ValueError('path must have positive length')
    return result


def include_stations(samples: list[Sample], fractions) -> list[Sample]:
    """Include every style keyframe, even a narrow peak between curve samples."""
    total = samples[-1].distance
    candidates = sorted({s.distance for s in samples} | {t * total for t in fractions})
    distances = []
    for distance in candidates:
        # Symmetric curves can put a station a few ULPs from an existing
        # sample. A second ring there would create zero-area mesh faces.
        if not distances or distance - distances[-1] > 1e-8:
            distances.append(distance)
    distances[-1] = total
    result, k = [], 1
    for distance in distances:
        while samples[k].distance < distance and k + 1 < len(samples):
            k += 1
        a, b = samples[k - 1], samples[k]
        t = (distance - a.distance) / (b.distance - a.distance)
        d = heading(
            (
                a.direction[0] + (b.direction[0] - a.direction[0]) * t,
                a.direction[1] + (b.direction[1] - a.direction[1]) * t,
                0,
            )
        )
        slope = a.slope if t == 0 else b.slope if t == 1 else None
        result.append(Sample(mix(a.point, b.point, t), d, distance, slope))
    return result


def stations(value, name: str, low: float, high: float) -> list[tuple[float, float]]:
    """A constant or sorted [fraction, value] stations, including 0 and 1."""
    if not isinstance(value, list):
        value = [[0, value], [1, value]]
    if not 2 <= len(value) <= 2048 or any(
        not isinstance(v, (list, tuple)) or len(v) != 2 for v in value
    ):
        raise ValueError(f'{name} needs [fraction, value] stations')
    result = [(number(t, name), number(v, name)) for t, v in value]
    if (
        result[0][0] != 0
        or result[-1][0] != 1
        or any(a[0] >= b[0] for a, b in zip(result, result[1:]))
    ):
        raise ValueError(f'{name} stations must increase from 0 to 1')
    if any(not low <= v <= high for _, v in result):
        raise ValueError(f'{name} must be within {low:g}..{high:g}')
    return result


def interpolate(keys: list[tuple[float, float]], t: float) -> float:
    for (a, va), (b, vb) in zip(keys, keys[1:]):
        if t <= b:
            return va + (vb - va) * (t - a) / (b - a)
    return keys[-1][1]
