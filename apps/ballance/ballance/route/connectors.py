"""Reusable authored transitions and the native platform mouth geometry."""

from ..core.geometry import at
from ..core.path import number
from ..kit.floors import RISE
from ..kit.rails import Ring
from ..kit.shapes import V_SECTION, FLAT_SECTION, section, sweep
from .ports import profile_height


def compile_paths(spec, before, layout):
    """Rail-to-floor endpoints reserve a straight five-unit standard V dock.

    The requested endpoint is the destination's entry, after the dock. This
    lets native funnels and polygon mouths consume their normal floor port.
    """
    from .endpoint import resolve
    from .ports import Section

    target = spec.get('to') if isinstance(spec, dict) else None
    docking = (
        isinstance(target, dict)
        and target.get('kind') == 'floor'
        and before.section.kind == 'rail'
        and 'endpoint' in spec
    )
    if not docking:
        return [transition(spec, before.section, before=before, layout=layout)]
    end, heading, slope = resolve(spec['endpoint'], before, layout)
    if abs(slope) > 1e-6:
        raise ValueError('rail-to-floor endpoint needs a level docking slope')
    rail_spec = {
        **spec,
        'endpoint': {
            'offset': [end[0] - 5 * heading[0], end[1] - 5 * heading[1], end[2]],
            'heading': list(heading),
            'slope': 0,
        },
        'to': {
            'kind': 'rail',
            'gauge': Section('rail').gauge,
            'radius': Section('rail').radius,
            'bank': 0,
        },
    }
    rail_path = transition(rail_spec, before.section, before=before, layout=layout)
    floor_path = transition({'length': 5, 'to': target}, Section('floor'))
    return [rail_path, floor_path]


def transition(spec, start, *, before=None, layout=None):
    """Compile an explicit same-kind connector to the existing sampled sweeper.

    The incoming section supplies defaults; no dimensions or bank are guessed.
    Rail/floor changes still use the native standard docking cross sections.
    """
    if not isinstance(spec, dict) or set(spec) - {
        'length',
        'segments',
        'to',
        'step',
        'tolerance',
        'ribs',
        'stops',
        'endpoint',
        'start_slope',
        'min_radius',
        'max_grade',
        'handles',
    }:
        raise ValueError(
            'connector accepts length, segments or endpoint, to, step, tolerance, ribs, stops '
            'and endpoint constraints (start_slope, min_radius, max_grade, handles)'
        )
    if sum(key in spec for key in ('length', 'segments', 'endpoint')) != 1:
        raise ValueError('connector needs exactly one of length, segments or endpoint')
    if 'endpoint' not in spec and set(spec) & {'start_slope', 'min_radius', 'max_grade', 'handles'}:
        raise ValueError('connector constraints require an endpoint')
    target = spec.get('to')
    allowed = {'kind', 'bank'} | (
        {'width', 'section', 'depth'} if start.kind == 'floor' else {'gauge', 'radius'}
    )
    if not isinstance(target, dict) or set(target) - allowed:
        raise ValueError(f'{start.kind} connector target accepts {sorted(allowed)}')
    if target.get('kind', start.kind) != start.kind:
        raise ValueError(
            'connector changes one cross section kind at a time; reach a '
            'standard port before docking rail/floor'
        )
    if 'length' in spec:
        length = number(spec['length'], 'connector length')
        if length <= 0:
            raise ValueError('connector length must be positive')
        segments = [{'line': [length, 0, 0]}]
    elif 'endpoint' in spec:
        from .endpoint import solve

        if before is None or layout is None:
            raise ValueError('endpoint connector needs an incoming port and layout')
        segments = solve(spec, before, layout)
    else:
        segments = spec['segments']
    path = {'segments': segments, 'bank': [[0, start.bank], [1, target.get('bank', start.bank)]]}
    path.update({key: spec[key] for key in ('step', 'tolerance', 'ribs', 'stops') if key in spec})
    if start.kind == 'rail':
        for key in ('gauge', 'radius'):
            path[key] = [[0, getattr(start, key)], [1, target.get(key, getattr(start, key))]]
    else:
        depth = number(target.get('depth', start.depth), 'connector depth')
        if abs(depth - start.depth) > 1e-6:
            raise ValueError('connector cannot change floor depth yet; keep matching depth')
        last = section(target.get('section', list(start.profile)))
        fractions = sorted({p[0] for p in (*start.profile, *last)})
        path.update(
            width=[[0, start.width], [1, target.get('width', start.width)]],
            depth=depth,
            section=[[u, profile_height(start.profile, u)] for u in fractions],
            end_section=[[u, profile_height(last, u)] for u in fractions],
        )
    return f'{start.kind}_path', path


def platform_mouth(position, heading, height, width, depth, *, leaving=False, distance=0):
    """Five-unit V/flat mouth, with a native 2.5-unit ProfileFlat material cell.

    The polygon owns its border and opening; this connector owns the neck's
    sides and bottom, with no internal caps. Width is shared by both owners.
    """
    middle, end = at(position, heading, 2.5), at(position, heading, 5)
    a = Ring(position[0], height + (RISE if leaving else 0), position[1], *heading)
    b = Ring(middle[0], height, middle[1], *heading)
    c = Ring(end[0], height + (0 if leaving else RISE), end[1], *heading)
    if leaving:
        intervals = ((a, b, FLAT_SECTION, V_SECTION, 0, 1), (b, c, V_SECTION, V_SECTION, 0, 0.5))
    else:
        intervals = (
            (a, b, V_SECTION, V_SECTION, distance / 5, (distance + 2.5) / 5),
            (b, c, V_SECTION, FLAT_SECTION, 0, 1),
        )
    return [
        (sweep, (0, 0), a, b, width, width, pa, pb, va, vb, depth)
        for a, b, pa, pb, va, vb in intervals
    ]
