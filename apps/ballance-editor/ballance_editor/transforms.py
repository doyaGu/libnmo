"""Atomic rigid transforms and compatible port alignment in authored space."""

from copy import deepcopy
from dataclasses import replace
import math
from ballance.core.path import vector, number
from ballance.route.ports import Port, Section, connect, ConnectionError, cardinal
from .model import locate, model


def selected(view, ids):
    if not isinstance(ids, list) or not ids or len(ids) > 4096 or len(ids) != len(set(ids)):
        raise ValueError('Select one or more distinct objects')
    entities = {e['id']: e for e in view['entities']}
    result = []
    for key in ids:
        e = entities.get(key)
        if e and e.get('edit_owner'):
            e = entities.get(e['edit_owner'])
        if e and e['id'].startswith('editor/rule/') and e['id'][12:] in entities:
            e = entities[e['id'][12:]]
        if (
            not e
            or not e.get('editable')
            or (
                not e.get('entry')
                and 'mechanism' not in e.get('edit_targets', {})
                and 'placement' not in e.get('edit_targets', {})
                and not e.get('details', {}).get('objects')
            )
        ):
            raise ValueError('Select authored route pieces or authored scenery instances')
        if not e.get('source', {}).get('stable'):
            raise ValueError('Assign stable IDs before placing objects')
        if e['id'] not in [a['id'] for a in result]:
            result.append(e)
    return result


def rotate(p, angle):
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    return [p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c]


def point(p, pivot, delta, angle):
    r = rotate([v - o for v, o in zip(p, pivot)], angle)
    return [o + v + d for o, v, d in zip(pivot, r, delta)]


def transform(route, view, data):
    items = selected(view, data['ids'])
    delta = vector(data.get('delta', [0, 0, 0]))
    angle = number(data.get('angle', 0), 'rotation')
    pivot = vector(data.get('pivot', items[0].get('entry', items[0]['end'])['position']))
    candidate = deepcopy(route)
    if any(e.get('entry') for e in items):
        # Freeze the current poses once; unselected successors must not drift.
        poses = candidate.setdefault('placements', {})
        for e in view['entities']:
            if e.get('entry') and e.get('source', {}).get('stable'):
                poses[e['id']] = deepcopy(e['entry'])
        for e in items:
            if not e.get('entry'):
                continue
            pose = poses[e['id']]
            h = rotate([pose['heading'][0], 0, pose['heading'][1]], angle)
            if (
                e['geometry_kind'] not in ('rail_path', 'floor_path', 'floor_polygon', 'connector')
                and cardinal((h[0], h[2])) is None
            ):
                raise ValueError('原版网格零件按 90° 旋转；自由轨道和自由地面可以使用任意角度。')
            pose['position'] = point(pose['position'], pivot, delta, angle)
            pose['heading'] = [h[0], h[2]]
    by_id = {e['id']: e for e in view['entities']}
    moving = {e['id'] for e in items}
    # Relative overrides follow their owner. A jointly selected child must not
    # receive the same transform twice.
    after = model(candidate) if any(e.get('entry') for e in items) else view
    after_by_id = {e['id']: e for e in after['entities']}
    for e in items:
        if e.get('entry'):
            continue
        if 'mechanism' in e['edit_targets']:
            details = after_by_id[e['id']]['details']
            base = details['base_pose']
            position = point(e['end']['position'], pivot, delta, angle)
            offset = rotate([v - o for v, o in zip(position, base['position'])], -base['yaw'])
            value = deepcopy(e['piece'])
            value.update(offset=offset, yaw=(e['details']['yaw'] + angle - base['yaw']) % 360)
            candidate.setdefault('mechanisms', {}).setdefault('instances', {})[e['id']] = value
            continue
        if 'placement' not in e['edit_targets']:
            rule = (
                candidate.setdefault('scenery', {})
                .setdefault('rules', {})
                .setdefault(e['id'].removeprefix('editor/rule/'), {})
            )
            rule.update(
                position=point(e['end']['position'], pivot, delta, angle),
                yaw=(e['details'].get('turn', 0) + angle) % 360,
                mode='attached' if e['details']['rule']['mode'] == 'attached' else 'locked',
                max_shift=0,
            )
            continue
        target = e['edit_targets']['placement']
        value = locate(candidate, target['pointer'])
        parent = by_id[value['parent']]
        position = point(e['end']['position'], pivot, delta, angle)
        end = parent['end']
        origin = end['position']
        heading = end['heading']
        if parent['id'] in moving:
            origin = point(origin, pivot, delta, angle)
            v = rotate([heading[0], 0, heading[1]], angle)
            heading = [v[0], v[2]]
        dx, dy, dz = [v - o for v, o in zip(position, origin)]
        hx, hz = heading
        value['at'] = [dx * hx + dz * hz, dx * hz - dz * hx, dy]
        if angle:
            value['yaw'] = (value.get('yaw', 0) + angle) % 360
        rule = candidate.setdefault('scenery', {}).setdefault('rules', {}).setdefault(e['id'], {})
        rule.update(mode='locked', max_shift=0)
    for value in candidate.get('scenery', {}).get('instances', []):
        if angle and value['parent'] in moving and 'scenery/id/' + value['id'] not in moving:
            value['yaw'] = (value.get('yaw', 0) + angle) % 360
    threshold = number(data.get('snap_distance', 0), 'snap distance')
    if threshold > 0:
        try:
            view_after = model(candidate)
            options = candidates(view_after, data['ids'])
            match = next(
                (p for p in options if p['distance'] <= threshold and abs(p['angle']) <= 15), None
            )
            if match:
                return snap(candidate, view_after, match)
        except ValueError:
            pass
    return candidate, items[0]['id']


def port(view, reference):
    e = next((e for e in view['entities'] if e['id'] == reference['entity']), None)
    name = reference.get('port')
    if not e or not (e.get('ports') or {}).get('snappable') or name not in ('entry', 'exit'):
        raise ValueError('Select a supported route entry or exit')
    data = e['ports'][name]
    return Port(
        e['id'], name, tuple(data['position']), tuple(data['heading']), Section(**data['section'])
    )


def alignment(view, data):
    moving = {e['id'] for e in selected(view, data['ids'])}
    source, target = port(view, data['source']), port(view, data['target'])
    if source.owner not in moving or target.owner in moving or source.name == target.name:
        raise ValueError('Snap a selected entry to an outside exit, or vice versa')
    # Test section, bank, gauge, width and depth without a distance restriction.
    a, b = (source, target) if source.name == 'exit' else (target, source)
    connect(a, replace(b, position=a.position, heading=a.heading))
    angle = math.degrees(
        math.atan2(source.heading[1], source.heading[0])
        - math.atan2(target.heading[1], target.heading[0])
    )
    return {
        'ids': data['ids'],
        'pivot': source.position,
        'delta': [b - a for a, b in zip(source.position, target.position)],
        'angle': angle,
    }


def snap(route, view, data):
    operation = alignment(view, data)
    return transform(route, view, operation)


def candidates(view, ids, limit=12):
    items = selected(view, ids)
    moving = {e['id'] for e in items}
    result = []
    # Internal joints in a selection are not docking targets.
    internal = {
        (c[side]['owner'], c[side]['name'])
        for c in view['connections']['connections']
        if c['from']['owner'] in moving and c['to']['owner'] in moving
        for side in ('from', 'to')
    }
    for e in items:
        if not (e.get('ports') or {}).get('snappable'):
            continue
        for name in ('entry', 'exit'):
            if (e['id'], name) in internal:
                continue
            for other in view['entities']:
                if other['id'] in moving or not (other.get('ports') or {}).get('snappable'):
                    continue
                ref = {
                    'ids': ids,
                    'source': {'entity': e['id'], 'port': name},
                    'target': {
                        'entity': other['id'],
                        'port': 'exit' if name == 'entry' else 'entry',
                    },
                }
                try:
                    operation = alignment(view, ref)
                except (ValueError, ConnectionError):
                    continue
                # Respect native-grid orientation even before the user clicks.
                if any(
                    item['geometry_kind']
                    not in ('rail_path', 'floor_path', 'floor_polygon', 'connector')
                    and abs(operation['angle'] / 90 - round(operation['angle'] / 90)) > 1e-6
                    for item in items
                    if item.get('entry')
                ):
                    continue
                result.append(
                    ref
                    | {
                        'distance': math.hypot(*operation['delta']),
                        'angle': operation['angle'],
                        'position': port(view, ref['target']).position,
                    }
                )
    return sorted(result, key=lambda r: r['distance'])[:limit]
