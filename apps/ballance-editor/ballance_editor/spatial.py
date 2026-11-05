"""Editor projections of authored settings and compiled spatial contracts.

Runtime state descriptions and measured envelopes are read-only. All writes go
through route authoring fields and the normal builder, including scenery fitting.
"""

from copy import deepcopy
import json

from ballance.game.resources import catalog
from ballance.route import attachments, motions
from ballance.route.identity import pointer
from ballance.route.spaces import part_id


def entity(identifier, label, category, sector, position, piece, path, scope):
    return {
        'id': identifier,
        'label': label,
        'geometry_kind': label,
        'category': category,
        'sector': sector,
        'end': {'position': position, 'heading': [1, 0]},
        'path': [],
        'source': {'pointer': path, 'stable': True, 'instances': []},
        'effective': piece,
        'editable': True,
        'overridden': False,
        'piece': piece,
        'edit_targets': {
            scope: {
                'piece': piece,
                'pointer': path,
                'label': {
                    'settings': '整体配置（可增删条目）',
                    'placement': '此装饰的位置与归属',
                    'rule': '此装饰的依附规则',
                    'mechanism': '仅此机关的放置与激活参数',
                }[scope],
            }
        },
    }


def extend(view, route, layout):
    entities = view['entities']
    parents = {e['id']: e for e in entities}
    descriptions = catalog()
    for item in entities:
        if item.get('piece') and 'resource' in item['piece']:
            name = item['piece']['resource']
            item['label'] = name
            item['details'] = {'resource': name, 'placement': descriptions[name]['placement']}
    view['mechanisms'] = motions.describe(layout)
    view['volumes'] = []
    append_mechanisms(view, parents)
    entities = view['entities']
    layout.resource_scenery = route.get('scenery', {}).get('instances', [])
    actors = attachments.authored(layout, {part_id(p): p for p in layout.parts})
    for index, (value, actor) in enumerate(zip(layout.resource_scenery, actors)):
        parent = parents[value['parent']]
        item = entity(
            actor['id'],
            value['resource'] + ' / ' + value['id'],
            'scenery',
            parent['sector'],
            actor['anchor'],
            value,
            f'/scenery/instances/{index}',
            'placement',
        )
        item['parent'] = value['parent']
        item['details'] = {
            'anchor': actor['anchor'],
            'parent': actor['parent'],
            'coordinates': '[along, right, up] from the parent exit',
        }
        entities.append(item)
    for key, label, category, default in (
        ('scenery', '装饰与依附配置', 'scenery', {}),
        ('mechanisms', '机关接触约束', 'constraint', {}),
        ('death_zones', '死亡区与恢复空间', 'constraint', None),
    ):
        value = deepcopy(route.get(key, default))
        item = entity(
            'editor/settings/' + key, label, category, 0, [0, 0, 0], value, '/' + key, 'settings'
        )
        item['global'] = True
        entities.append(item)
    return view


def append_mechanisms(view, parents):
    # Compiled records replace the provisional state descriptions after native
    # stopper resolution. Runtime instances keep their own editable identity.
    view['entities'] = [e for e in view['entities'] if e.get('category') != 'mechanism']
    for mechanism in view['mechanisms']['instances']:
        # Older history reports predate authored instance settings. Their
        # compiled marker pose is the unmodified installation frame.
        mechanism = {
            'settings': {'offset': [0, 0, 0], 'yaw': 0, 'sector': mechanism['sector']},
            'base_pose': {k: mechanism[k] for k in ('position', 'yaw', 'sector')},
        } | mechanism
        parent = parents.get(mechanism['parent'])
        if not parent:
            continue
        path = '/mechanisms/instances/' + pointer(mechanism['id'])
        item = entity(
            mechanism['id'],
            mechanism['kind'],
            'mechanism',
            parent['sector'],
            mechanism['position'],
            mechanism['settings'],
            path,
            'mechanism',
        )
        item.update(parent=parent['id'], details=mechanism, editable=parent['editable'])
        item['source']['stable'] = parent['source']['stable']
        view['entities'].append(item)


def compiled(view, folder):
    """Attach the exact fitted positions and envelopes from one generation."""
    path = folder / 'spatial_constraints.json'
    if not path.is_file():
        return view
    spatial = json.loads(path.read_text())
    parents = {e['id']: e for e in view['entities']}
    view['mechanisms'] = spatial['mechanisms']
    append_mechanisms(view, parents)
    view['volumes'] = spatial['occupancy']
    objects = {
        o['name']: o
        for o in json.loads((folder / 'manifest.json').read_text())['scenes'][0]['objects']
    }
    from ballance.route.clashes import _placed

    for binding in spatial['attachments']:
        parent = parents.get(binding['parent'])
        sector = parent['sector'] if parent else binding.get('sector', 0)
        if binding['parent'] == 'marker/start':
            sector = 1
        elif binding['parent'] == 'marker/finish':
            sector = view['sectors']
        elif binding['parent'].startswith('marker/checkpoint/'):
            sector = min(view['sectors'], int(binding['parent'].rsplit('/', 1)[1]) + 2)
        position = binding['position'] or binding['anchor']
        for name in binding['objects']:
            obj = objects[name]
            transform = obj['transform']
            box = _placed(
                folder / obj['mesh']['obj'],
                transform['position'],
                transform.get('rotation_euler_deg', [0, 0, 0])[1],
            )
            if box:
                view['volumes'].append(
                    {
                        'owner': binding['id'],
                        'rule': 'scenery assembly',
                        'box': box,
                        'state': 'always',
                        'source': {'sector': sector, 'parent': binding['parent']},
                    }
                )
        item = parents.get(binding['id'])
        if item:
            item['end']['position'] = position
            item['details']['binding'] = binding
        # Poles have a pair-level symmetry contract, edited via finish_poles.
        if binding['role'] == 'pole':
            continue
        rule = entity(
            'editor/rule/' + binding['id'],
            '依附 / ' + binding['id'],
            'scenery',
            sector,
            position,
            binding['rule'],
            '/scenery/rules/' + pointer(binding['id']),
            'rule',
        )
        rule['parent'] = binding['parent']
        rule['details'] = binding
        view['entities'].append(rule)
        if item:
            item['edit_targets']['rule'] = rule['edit_targets']['rule']
    zones = json.loads((folder / 'death_zones.json').read_text())
    view['volumes'] += [
        {
            'owner': 'editor/settings/death_zones',
            'rule': 'death zone',
            'box': box,
            'state': 'always',
            'source': {},
        }
        for box in zones['boxes']
    ]
    view['spatial_basis'] = (
        'Compiled native envelopes and fitted scenery; phase selection is '
        'inspection, not simulation.'
    )
    return view


def edit_setting(route, selected, value, scope, target):
    if scope == 'settings':
        key = target['pointer'][1:]
        if key not in ('scenery', 'mechanisms', 'death_zones'):
            raise ValueError('Unknown editable settings')
        if not isinstance(value, dict) and not (key == 'death_zones' and value is None):
            raise ValueError(
                'Settings must be an object (death_zones also accepts null for legacy zones)'
            )
        route[key] = deepcopy(value)
    elif scope == 'mechanism':
        route.setdefault('mechanisms', {}).setdefault('instances', {})[selected['id']] = deepcopy(
            value
        )
    elif scope == 'placement':
        if not isinstance(value, dict) or value.get('id') != target['piece']['id']:
            raise ValueError('Keep the scenery instance ID unchanged')
        from .model import replace_at

        replace_at(route, target['pointer'], value)
    else:
        if not isinstance(value, dict):
            raise ValueError('An attachment rule must be an object')
        identifier = target['pointer'].rsplit('/', 1)[1].replace('~1', '/').replace('~0', '~')
        route.setdefault('scenery', {}).setdefault('rules', {})[identifier] = deepcopy(value)
