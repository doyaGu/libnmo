"""Native mechanism instances and their state-dependent spatial reservations."""

import json
from functools import lru_cache
from pathlib import Path

from ..core.motion import place, rotation_boxes
from ..core.space import Volume, bounds, inflate
from .spaces import part_id
from ..game.resources import mechanisms

CATALOG = Path(__file__).resolve().parents[2] / 'data/motion_catalog.json'
SCOPE = (
    'Per-body native contracts include unbounded free bodies and unlimited sliders. '
    'Measured native hinge sweeps, free-joint reach and fan force fields. '
    'Finite envelopes are reserved for every phase regardless of '
    'sector; unbounded motion is reported separately. '
    'Conservative intersections need review; states describe native '
    'activation, not observed completion.'
)


def catalog():
    return json.loads(CATALOG.read_text())['recipes']


def instances(layout):
    if not hasattr(layout, 'mechanism_instances'):
        from .mechanism_placement import prepare

        prepare(layout)
    recipes = catalog()
    descriptions = mechanisms()
    result = []
    for part in layout.parts:
        counts = {}
        for kind, sector, position, *rotation in [*part.modules, *part.elements]:
            if not kind.startswith('P_Modul_'):
                continue
            index = counts.get(kind, 0)
            counts[kind] = index + 1
            owner = part_id(part)
            identifier = f'{owner}/mechanism/{kind}/{index}'
            recipe = recipes.get(kind)
            states = (
                (
                    descriptions[kind][1]['runtime']['states']
                    if kind in descriptions
                    else ['inactive', 'ready', 'moving']
                )
                if recipe
                else []
            )
            result.append(
                {
                    'id': identifier,
                    'parent': owner,
                    'kind': kind,
                    'sector': sector,
                    'position': position,
                    'yaw': rotation[0] if rotation else 0,
                    'source': part.origin.data() if part.origin else None,
                    'states': states,
                    'initial': 'inactive',
                    'transitions': (
                        [
                            {
                                'from': 'inactive',
                                'to': 'ready',
                                'when': 'sector activated; restore initial conditions',
                            },
                            {
                                'from': 'ready',
                                'to': 'moving',
                                'when': 'native drive or physical contact',
                            },
                            {
                                'from': 'moving',
                                'to': 'ready',
                                'when': 'sector reset; restore initial conditions',
                            },
                            {'from': 'ready', 'to': 'inactive', 'when': 'sector deactivated'},
                            {'from': 'moving', 'to': 'inactive', 'when': 'sector deactivated'},
                        ]
                        if recipe
                        else []
                    ),
                    'coverage': 'modeled' if recipe else 'unsupported',
                }
            )
            if recipe and recipe['type'] == 'assembly':
                result[-1]['states'] = recipe['states']
                result[-1]['transitions'].extend(recipe['extra_transitions'])
            result[-1].update(layout.mechanism_instances[identifier])
            result[-1]['stops'] = getattr(layout, 'motion_limits', {}).get(identifier)
    return result


@lru_cache(maxsize=128)
def _volumes(kind, position, yaw, recipe_text):
    recipe = json.loads(recipe_text)
    result = []
    for name, body in recipe['bodies'].items():
        rule = 'fan force field' if name == recipe.get('effect_body') else 'mechanism initial pose'
        result.append(
            (rule, 'ready', bounds([place(p, position, yaw) for p in body['points']]), name)
        )
    if recipe['type'] == 'assembly':
        for name, contract in recipe['contracts'].items():
            points = recipe['bodies'][name]['points']
            if contract['type'] == 'hinge':
                boxes = rotation_boxes(
                    points, contract['pivot'], contract['axis'], position, yaw, -180, 180
                )
            elif contract['type'] == 'reach':
                p = place(contract['pivot'], position, yaw)
                r = contract['radius']
                boxes = [tuple(v + s * r for v in p for s in (-1, 1))]
            elif contract['type'] == 'fixed':
                boxes = [bounds([place(p, position, yaw) for p in points])]
            else:
                continue  # No finite global AABB; reported by unbounded().
            margin = solver_margin(recipe, contract)
            boxes = [inflate(box, margin) for box in boxes]
            for phase in recipe['states']:
                if phase in ('inactive', 'ready'):
                    continue
                result.extend(('mechanism ' + contract['type'], phase, box, name) for box in boxes)
    elif recipe['type'] == 'ball_joint':
        p = place(recipe['pivot'], position, yaw)
        r = recipe['radius']
        result.append(
            (
                'mechanism free-joint reach',
                'moving',
                tuple(v + s * r for v in p for s in (-1, 1)),
                kind,
            )
        )
    else:
        body = recipe['bodies'][recipe['body']]
        boxes = rotation_boxes(
            body['points'], recipe['pivot'], recipe['axis'], position, yaw, *recipe['angles']
        )
        result.extend(('mechanism sweep', 'moving', box, recipe['body']) for box in boxes)
        if recipe['type'] == 'fan':
            body = recipe['bodies'][recipe['effect_body']]
            result.append(
                (
                    'fan force field',
                    'moving',
                    bounds([place(p, position, yaw) for p in body['points']]),
                    recipe['effect_body'],
                )
            )
    return result


def volumes(instance, recipe):
    if recipe is None:
        return []
    from .dynamics import joint_angles

    if recipe['type'] == 'hinge':
        recipe = recipe | {'angles': joint_angles(instance, recipe)}
    source = {
        'entity_id': instance['id'],
        'parent': instance['parent'],
        'source': instance['source'],
        'sector': instance['sector'],
        'piece': instance['kind'],
        'branch': None,
    }
    result = []
    for rule, state, box, body in _volumes(
        instance['kind'],
        tuple(instance['position']),
        instance['yaw'],
        json.dumps(recipe, sort_keys=True),
    ):
        reach = (
            recipe if recipe['type'] == 'ball_joint' else recipe.get('contracts', {}).get(body, {})
        )
        sphere = state not in ('ready', 'inactive') and (
            recipe['type'] == 'ball_joint' or reach.get('type') == 'reach'
        )
        result.append(
            Volume(
                instance['id'],
                rule,
                box,
                state=f'{instance["id"]}/{state}',
                severity='review',
                centre=place(reach['pivot'], instance['position'], instance['yaw'])
                if sphere
                else None,
                radius=reach['radius'] + solver_margin(recipe, reach) if sphere else 0,
                source={**source, 'phase': state, 'body': body},
            )
        )
    return result


def add_to(space, layout):
    recipes = catalog()
    for instance in instances(layout):
        for volume in volumes(instance, recipes.get(instance['kind'])):
            space.add(volume)


def describe(layout):
    recipes = catalog()
    items = instances(layout)
    templates = {
        i['kind']: {k: recipes[i['kind']][k] for k in ('template', 'sha256', 'basis')}
        for i in items
        if i['kind'] in recipes
    }
    for item in items:
        if item.get('stops'):
            templates['Levelinit'] = recipes[item['kind']]['stop_runtime']
    return {
        'schema_version': 1,
        'scope': SCOPE,
        'instances': items,
        'interactions': interactions(layout, items),
        'templates': templates,
        'unbounded_motion': [c for i in items for c in unbounded(i, recipes.get(i['kind']))],
    }


def interactions(layout, items=None):
    settings = getattr(layout, 'mechanism_settings', {})
    if not isinstance(settings, dict) or set(settings) - {'interactions', 'instances'}:
        raise ValueError('mechanisms accepts interactions and instances only')
    values = settings.get('interactions', [])
    if not isinstance(values, list):
        raise ValueError('mechanism interactions must be a list')
    known = {i['id'] for i in (items if items is not None else instances(layout))}
    targets = {part_id(p) for p in layout.parts}
    seen = set()
    for value in values:
        if not isinstance(value, dict) or set(value) != {
            'mechanism',
            'target',
            'contacts',
            'phases',
            'reason',
        }:
            raise ValueError('an interaction needs mechanism, target, contacts, phases and reason')
        if (
            not isinstance(value['mechanism'], str)
            or value['mechanism'] not in known
            or not isinstance(value['target'], str)
            or value['target'] not in targets
        ):
            raise ValueError('interaction references an unknown mechanism or route entity')
        if not isinstance(value['reason'], str) or not value['reason'].strip():
            raise ValueError('interaction needs a design reason')
        for field, allowed in [
            ('contacts', {'rail', 'ball clearance', 'route solid'}),
            ('phases', {'ready', 'moving', 'split'}),
        ]:
            entries = value[field]
            if (
                not isinstance(entries, list)
                or not entries
                or any(not isinstance(e, str) or e not in allowed for e in entries)
                or len(set(entries)) != len(entries)
            ):
                raise ValueError(f'invalid interaction {field}')
        pair = value['mechanism'], value['target']
        if pair in seen:
            raise ValueError('duplicate mechanism interaction pair')
        seen.add(pair)
    return values


def intention(rules, mechanism, target, contact, phase):
    return next(
        (
            r['reason']
            for r in rules
            if r['mechanism'] == mechanism
            and r['target'] == target
            and contact in r['contacts']
            and phase in r['phases']
        ),
        None,
    )


def template_paths(game):
    if game is None:
        return []
    recipes = list(catalog().values())
    return sorted(
        {game / r['template'] for r in recipes}
        | {game / r['stop_runtime']['template'] for r in recipes if 'stop_runtime' in r}
    )


def unbounded(instance, recipe):
    """Explicit coverage limits: never silently omit an infinite reservation."""
    return [
        {
            'mechanism': instance['id'],
            'body': name,
            'type': c['type'],
            'phase': 'moving',
            'reason': 'No native hard travel limit; contact and forces do not establish '
            'a finite bound.',
        }
        for name, c in (recipe or {}).get('contracts', {}).items()
        if c['type'] in ('free', 'slider')
    ]


def accepts_pose(instance, recipe, name, points, space, tolerance=0.1):
    """Validate one body, without using a neighbour's reservation as cover."""
    import math
    from ..core.space import inflate

    contract = recipe.get('contracts', {}).get(name, {})
    tolerance += solver_margin(recipe, contract)
    if contract.get('type') == 'free':
        return True  # Unbounded is explicit in both static and trace reports.
    if contract.get('type') == 'slider':
        origin = instance['position']
        yaw = instance['yaw']
        axis = place(contract['axis'], (0, 0, 0), yaw)
        expected = [place(p, origin, yaw) for p in recipe['bodies'][name]['points']]
        deltas = [[p[k] - q[k] for k in range(3)] for p, q in zip(points, expected)]
        travel = sum(deltas[0][k] * axis[k] for k in range(3))
        return all(
            math.sqrt(sum((d[k] - travel * axis[k]) ** 2 for k in range(3))) <= tolerance
            for d in deltas
        )
    if contract.get('type') == 'reach':
        centre = place(contract['pivot'], instance['position'], instance['yaw'])
        return all(math.dist(p, centre) <= contract['radius'] + tolerance for p in points)
    return all(space.query([inflate(bounds([p]), 0.1)], first=True) for p in points)


def solver_margin(recipe, contract):
    if contract.get('type') not in ('hinge', 'reach', 'slider'):
        return 0.0
    return recipe.get('solver_tolerance', 0.0) * max(1, len(contract.get('joint_path', [])))
