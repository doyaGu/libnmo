"""Bind native resources to route owners, scene objects and attachment outcomes."""

from ..game.resources import catalog, elements, assemblies, scenery_resource
from ..core.motion import place
from ..core.geometry import turn_of
from .spaces import part_id
from .modules import MODULE_FLOORS


def _instances(layout, descriptions):
    result = []
    static = assemblies()
    for part in layout.parts:
        counts = {}
        for kind, sector, position, yaw in part.module_floors:
            if kind not in static:
                continue  # These surfaces belong to a native mechanism instance.
            name, _ = static[kind]
            index = counts.get(name, 0)
            counts[name] = index + 1
            identifier = f'{part_id(part)}/resource/{name}' + (f'/{index}' if index else '')
            result.append(
                {
                    'id': identifier,
                    'parent': part_id(part),
                    'resource': name,
                    'sector': sector,
                    'position': position,
                    'yaw': yaw,
                    'source': part.origin.data() if part.origin else None,
                }
            )
    if layout.balloon:
        position, heading = layout.balloon
        result.append(
            {
                'id': 'finish/balloon',
                'parent': 'finish/balloon',
                'resource': 'Balloon',
                'sector': len(layout.sectors),
                'position': position,
                'yaw': turn_of(heading) + descriptions['Balloon']['placement']['yaw_offset'],
                'source': {'kind': 'generated finish'},
            }
        )
    known = elements()
    for part in layout.parts:
        counts = {}
        for kind, sector, position, *rotation in [*part.modules, *part.elements]:
            if kind not in known:
                raise ValueError(f'{kind}: native element has no unified resource description')
            name, spec = known[kind]
            index = counts.get(kind, 0)
            counts[kind] = index + 1
            role = 'mechanism' if spec['category'] == 'mechanism' else 'element'
            identifier = f'{part_id(part)}/{role}/{kind}/{index}'
            instance = {
                'id': identifier,
                'parent': part_id(part),
                'resource': name,
                'sector': sector,
                'position': position,
                'yaw': rotation[0] if rotation else 0,
                'source': part.origin.data() if part.origin else None,
            }
            if role == 'mechanism':
                instance.update(layout.mechanism_instances[identifier])
                instance['mechanism'] = identifier
                instance['motion_coverage'] = (
                    'modeled' if spec['runtime'].get('motion_recipe') else 'unsupported'
                )
            if 'variants' in spec:
                embedded = spec['variants']['embedded']['surface_owner_module']
                instance['variant'] = (
                    'embedded'
                    if any(f[0] == embedded for f in part.module_floors)
                    else 'standalone'
                )
                if instance['variant'] == 'embedded':
                    instance['surface_owner'] = part_id(part)
            result.append(instance)
    for binding in layout.scenery_bindings:
        if binding['role'] not in ('lamp', 'pillar', 'pole'):
            continue
        name, _ = scenery_resource(binding['role'])
        result.append(
            {
                'id': binding['id'],
                'parent': binding['parent'],
                'resource': name,
                'position': binding['position'] or binding['anchor'],
                'yaw': binding['turn'],
                'source': binding['source'],
                'binding': binding['id'],
            }
        )
    return result


def _matches(obj, mesh, position, yaw):
    transform = obj.get('transform', {})
    return (
        obj.get('mesh', {}).get('obj') == mesh
        and transform.get('position') == position
        and transform.get('rotation_euler_deg', [0, 0, 0])[1] % 360 == yaw % 360
    )


def describe(layout, assets, manifest):
    descriptions = catalog()
    instances = _instances(layout, descriptions)
    parents = {part_id(p): p for p in layout.parts}
    objects = manifest['scenes'][0]['objects']
    for instance in instances:
        spec = descriptions[instance['resource']]
        instance['ports'] = {
            name: {
                **port,
                'position': place(port['position'], instance['position'], instance['yaw']),
                'direction': place(port['direction'], (0, 0, 0), instance['yaw']),
            }
            for name, port in spec['ports'].items()
        }
        owners = {instance['parent']}
        if instance['resource'] == 'Balloon':
            owners.add('marker/finish')
        bindings = (
            [b for b in layout.scenery_bindings if b['id'] == instance['binding']]
            if 'binding' in instance
            else [b for b in layout.scenery_bindings if b['parent'] in owners]
        )
        instance['attachments'] = [
            {k: b[k] for k in ('id', 'kinds', 'action', 'objects', 'position', 'rule')}
            for b in bindings
        ]
        instance['components'] = []
        for component in spec['parts']:
            if (
                'variant' in instance
                and component['name'] not in spec['variants'][instance['variant']]['parts']
            ):
                continue
            mesh = assets.mesh(component['name'])
            frame = (
                instance.get('base_pose', instance)
                if component['role'] != 'placement_marker'
                else instance
            )
            frames = [(frame['position'], frame['yaw'])]
            if component.get('placement_frame') == 'parent_module_floor':
                frames = [
                    (p, yaw)
                    for kind, _, p, yaw in parents[instance['parent']].module_floors
                    if any(f.name == component['name'] for f in MODULE_FLOORS[kind])
                ]
            members = [
                o['name'] for o in objects if any(_matches(o, mesh, p, yaw) for p, yaw in frames)
            ]
            if 'binding' in instance or component['support']:
                members = [
                    o['name']
                    for b in bindings
                    if component['name'] in b['kinds']
                    for o in objects
                    if o['name'] in b['objects'] and o.get('mesh', {}).get('obj') == mesh
                ]
            omitted = spec.get('attachments', {}).get(component['name'], {}).get(
                'optional', False
            ) or any(b['position'] is None for b in bindings)
            instance['components'].append(
                {
                    'part': component['name'],
                    'role': component['role'],
                    'objects': members,
                    'status': 'placed'
                    if members
                    else 'omitted'
                    if omitted
                    else 'stand-in'
                    if not assets.game
                    else 'missing',
                }
            )
    bound = {o for i in instances for c in i['components'] for o in c['objects']}
    # Markers, automatic pads, fan housings and sky have their own transforms,
    # not the enclosing route part's frame (start flames even rotate separately).
    automatic = {
        s['parts'][0]['name'] + '.obj': (name, s)
        for name, s in descriptions.items()
        if len(s['parts']) == 1 and 'generated_by' in s.get('placement', {})
    }
    for obj in objects:
        mesh = obj.get('mesh', {}).get('obj')
        if obj['name'] in bound or mesh not in automatic:
            continue
        name, spec = automatic[mesh]
        component = spec['parts'][0]
        instances.append(
            {
                'id': 'scene/' + obj['name'],
                'parent': 'level',
                'resource': name,
                'position': obj['transform']['position'],
                'yaw': obj['transform'].get('rotation_euler_deg', [0, 0, 0])[1],
                'source': {'kind': 'generated scene object'},
                'ports': {},
                'attachments': [],
                'components': [
                    {
                        'part': component['name'],
                        'role': component['role'],
                        'objects': [obj['name']],
                        'status': 'placed',
                    }
                ],
            }
        )
        bound.add(obj['name'])
    native_meshes = {p['name'] + '.obj' for s in descriptions.values() for p in s['parts']}
    unbound = [
        o['name']
        for o in objects
        if o.get('mesh', {}).get('obj') in native_meshes and o['name'] not in bound
    ]
    return {
        'schema_version': 1,
        'native_assets': assets.game,
        'resources': {
            name: {'description': descriptions[name], 'native': assets.resource_records.get(name)}
            for name in sorted({i['resource'] for i in instances})
        },
        'instances': instances,
        'unbound_native_objects': unbound if assets.game else [],
        'scope': 'All native meshes exported by the Ballance element tool have '
        'resource descriptions. '
        'Resource provenance and placement coverage do not imply measured motion coverage; '
        'unsupported mechanisms retain that status in motion checks.',
    }
