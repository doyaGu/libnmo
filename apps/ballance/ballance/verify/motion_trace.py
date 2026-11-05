"""Observed physical poses, separate from declared state reachability.

This trace reports movement and conservative pose contacts. It does not infer
gameplay success, switches firing, or absence of between-frame collisions.
"""

import csv
import math
import re

from ..core.motion import corners, place
from ..core.nmo import place as matrix_place, into
from ..core.space import bounds, Space
from ..route import motions
from ..route.spaces import route_space


def inspect(path, layout, finish=None):
    if not path.is_file():
        return {'status': 'unavailable', 'reason': 'No physical body trace.'}
    if (path.parent / 'manifest.json').is_file():
        from ..route.assets import Assets
        from ..route.motion_stops import prepare

        prepare(layout, Assets.existing(path.parent))
    recipes = motions.catalog()
    rules = motions.interactions(layout)
    items = motions.instances(layout)
    candidates = []
    for instance in items:
        recipe = recipes.get(instance['kind'])
        if not recipe:
            continue
        for name, body in recipe['bodies'].items():
            candidates.append(
                (
                    instance,
                    recipe,
                    name,
                    body,
                    place(body['matrix'][12:15], instance['position'], instance['yaw']),
                )
            )
    routes = route_space(layout, mechanisms=False)
    assigned, records, contacts, escapes = {}, {}, {}, {}
    previous_frame = 0
    poses = {}
    splits = {}

    def observe_splits(frame):
        for instance in items:
            recipe = recipes.get(instance['kind'], {})
            for joint in recipe.get('joints', []):
                if not joint.get('breaks_in_play'):
                    continue
                names = [joint['body'], joint['other']]
                if any((instance['id'], name) not in poses for name in names):
                    continue
                anchors = [
                    matrix_place(
                        poses[instance['id'], name],
                        into(recipe['bodies'][name]['matrix'], joint['pivot']),
                    )
                    for name in names
                ]
                gap = math.dist(*anchors)
                # Separation beyond both ends' numerical allowance proves
                # the joint no longer holds; it does not infer the trigger.
                if gap > 2 * recipe['solver_tolerance']:
                    key = (instance['id'], joint['id'])
                    record = splits.setdefault(
                        key,
                        {
                            'mechanism': instance['id'],
                            'state': 'split',
                            'joint': joint['id'],
                            'first_frame': frame,
                            'max_anchor_separation': 0.0,
                        },
                    )
                    record['max_anchor_separation'] = max(record['max_anchor_separation'], gap)

    rows = 0
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ['frame', 'id', 'name', *[f'm{k}' for k in range(16)]]:
            raise ValueError('invalid physical body trace header')
        for row in reader:
            frame = int(row['frame'])
            if frame < previous_frame:
                raise ValueError('physical body trace frames are not ordered')
            if frame != previous_frame:
                observe_splits(previous_frame)
                poses.clear()
            previous_frame = frame
            if finish is not None and frame > finish:
                break
            rows += 1
            identifier = row['id']
            if identifier not in assigned:
                matching = [
                    c for c in candidates if re.fullmatch(re.escape(c[2]) + r'\d*', row['name'])
                ]
                if not matching:
                    continue
                position = tuple(float(row[f'm{k}']) for k in (12, 13, 14))
                matching.sort(key=lambda c: math.dist(position, c[4]))
                if math.dist(position, matching[0][4]) > 1:
                    continue  # template objects parked at the origin are not level instances
                if (
                    len(matching) > 1
                    and abs(
                        math.dist(position, matching[0][4]) - math.dist(position, matching[1][4])
                    )
                    < 1e-5
                ):
                    raise ValueError(f'ambiguous mechanism body {row["name"]}')
                assigned[identifier] = matching[0]
            instance, recipe, name, body, _ = assigned[identifier]
            matrix = [float(row[f'm{k}']) for k in range(16)]
            if not all(math.isfinite(v) for v in matrix):
                raise ValueError('nonfinite physical body transform')
            points = [matrix_place(matrix, p) for p in corners(body['mesh_box'])]
            key = (instance['id'], name)
            poses[key] = matrix
            if key not in records:
                space = Space()
                for volume in motions.volumes(instance, recipe):
                    if volume.source['body'] == name or recipe['type'] == 'ball_joint':
                        space.add(volume)
                records[key] = {
                    'mechanism': instance['id'],
                    'body': name,
                    'runtime_ids': [],
                    'envelope_type': recipe.get('contracts', {})
                    .get(name, {})
                    .get('type', recipe['type']),
                    'solver_margin': motions.solver_margin(
                        recipe, recipe.get('contracts', {}).get(name, {})
                    ),
                    'observed_box': bounds(points),
                    'samples': 0,
                    'first_frame': frame,
                    'last_frame': frame,
                    'first_displaced_frame': None,
                    'max_corner_displacement': 0.0,
                    'active_ranges': [],
                    'space': space,
                }
            record = records[key]
            if identifier not in record['runtime_ids']:
                record['runtime_ids'].append(identifier)
            if not record['active_ranges'] or record['active_ranges'][-1][1] < frame - 1:
                record['active_ranges'].append([frame, frame])
            else:
                record['active_ranges'][-1][1] = frame
            expected = [place(p, instance['position'], instance['yaw']) for p in body['points']]
            distance = max(math.dist(p, q) for p, q in zip(points, expected))
            if distance > 0.1 and record['first_displaced_frame'] is None:
                record['first_displaced_frame'] = frame
            record['max_corner_displacement'] = max(distance, record['max_corner_displacement'])
            old = record['observed_box']
            box = bounds(points)
            record['observed_box'] = tuple(
                (min if k % 2 == 0 else max)(old[k], box[k]) for k in range(6)
            )
            record['samples'] += 1
            record['last_frame'] = frame
            # Tolerance accommodates single-precision native transforms and
            # small joint solver drift. Full-pose extrema are checked statically.
            if key not in escapes and not motions.accepts_pose(
                instance, recipe, name, points, record['space']
            ):
                escapes[key] = {'mechanism': instance['id'], 'body': name, 'frame': frame}
            for hit in routes.query([bounds(points)], ignore=(instance['parent'],)):
                phase = 'moving' if distance > 0.1 else 'ready'
                reason = motions.intention(rules, instance['id'], hit['owner'], hit['kind'], phase)
                contact_key = (instance['id'], name, hit['owner'], hit['kind'], phase)
                if contact_key not in contacts:
                    contacts[contact_key] = {
                        'mechanism': instance['id'],
                        'body': name,
                        'contact': hit,
                        'phase': phase,
                        'design_reason': reason,
                        'first_frame': frame,
                        'last_frame': frame,
                        'samples': 0,
                    }
                contacts[contact_key]['last_frame'] = frame
                contacts[contact_key]['samples'] += 1
    observe_splits(previous_frame)
    for record in records.values():
        del record['space']
    observed = {r['mechanism'] for r in records.values()}
    expected = [c for c in contacts.values() if c['design_reason']]
    findings = [c for c in contacts.values() if not c['design_reason']]
    return {
        'status': 'review' if escapes or findings else 'passed' if records else 'unavailable',
        'observed_states': list(splits.values()),
        'unbounded_motion': [
            c for i in items for c in motions.unbounded(i, recipes.get(i['kind']))
        ],
        'rows': rows,
        'observed_instances': len(observed),
        'bodies': list(records.values()),
        'unobserved': [i['id'] for i in items if i['id'] not in observed],
        'envelope_escapes': list(escapes.values()),
        'contacts': findings,
        'expected_interactions': expected,
        'scope': 'Physicalized bodies sampled after every simulation step through finish. '
        'Observed boxes cover recorded samples only, not all future '
        'poses. Free bodies have no finite bound. '
        'Pose AABB contacts are conservative; no inference of mechanism '
        'completion or reset success. '
        'Nonphysical fan rotors and force fields are checked by their static recipes only.',
    }
