"""Measure motion recipes from local PH templates, without copying meshes."""

import hashlib
import math
import tempfile
from pathlib import Path

from ..core.nmo import field_values, matrix_of, place
from ..core.space import bounds
from ..core.motion import corners

# Physics hinge axes are the referential's Z axis. These native hinges have
# Limitations=false; contact with their own floors is not an angular limit.
ASSEMBLIES = [f'P_Modul_{n:02d}' for n in (1, 3, 8, 19, 29, 30, 34)]

HINGES = {
    'P_Modul_17': ('Modul17_Dreharme', 'P_Modul_17_HingeFrame'),
    'P_Modul_25': ('P_Modul_25_Bridge', 'P_Modul_25_HingeFrame'),
    'P_Modul_37': ('P_Modul_37_Bridge', 'P_Modul_37_HingeFrame'),
    'P_Modul_41': ('P_Modul_41', 'P_Modul_41_HingeFrame'),
}


def measure(game, nmo):
    recipes = {}
    for kind in [*HINGES, 'P_Modul_26', 'P_Modul_18', *ASSEMBLIES]:
        path = Path(game) / '3D Entities/PH' / f'{kind}.nmo'
        entities = {o['name']: field_values(o) for o in nmo.objects(path, 'CK3dEntity')}
        bodies = {}
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            nmo.export_meshes(path, folder)
            for name, f in entities.items():
                if not f.get('current_mesh'):
                    continue
                mesh = next(folder.glob(f'*_{f["current_mesh"]}.obj'))
                vertices = [
                    tuple(map(float, line.split()[1:4]))
                    for line in mesh.read_text().splitlines()
                    if line.startswith('v ')
                ]
                matrix = matrix_of(f['world_matrix'])
                bodies[name] = {
                    'mesh_box': bounds(vertices),
                    'matrix': matrix,
                    'points': [place(matrix, p) for p in corners(bounds(vertices))],
                }
            # Read the switch's actual convex shapes to prove native stops.
            if kind == 'P_Modul_37':
                blocks = nmo.data('behavior', 'find', '--name', 'Physicalize', str(path))['results']
                if len(blocks) != 1:
                    raise ValueError('switch recipe requires one physicalized body')
                block = nmo.data('behavior', 'show', str(blocks[0]['id']), str(path))
                parameters = {
                    o['id']: field_values(o) for o in nmo.objects(path, 'CKParameterLocal')
                }
                hulls = []
                for parameter in block['input_parameters']:
                    if not parameter['name'].startswith('convex '):
                        continue
                    mesh_id = parameters[parameter['source_id']]['object_ref']
                    mesh = next(folder.glob(f'*_{mesh_id}.obj'))
                    vertices = sorted(
                        set(
                            tuple(map(float, line.split()[1:4]))
                            for line in mesh.read_text().splitlines()
                            if line.startswith('v ')
                        )
                    )
                    hulls.append({'mesh_id': mesh_id, 'points': vertices})
                if len(hulls) != 3:
                    raise ValueError(
                        'switch recipe requires its three native convex collision shapes'
                    )
                bodies['P_Modul_37_Bridge']['collision_hulls'] = hulls
        recipe = {
            'template': f'3D Entities/PH/{kind}.nmo',
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'bodies': bodies,
        }
        if kind == 'P_Modul_37':
            levelinit = Path(game) / '3D Entities/Levelinit.nmo'
            recipe['stop_runtime'] = {
                'template': '3D Entities/Levelinit.nmo',
                'sha256': hashlib.sha256(levelinit.read_bytes()).hexdigest(),
                'basis': 'Native floors and stoppers physicalization; stoppers use '
                'no-collision group ball.',
            }
        if kind in ASSEMBLIES:
            recipe.update(assembly(path, nmo, bodies, entities))
        elif kind in HINGES:
            limits = [
                o
                for o in nmo.objects(path, 'CKParameterLocal')
                if o.get('name') == 'Limitations (-180 to 180 degree)'
            ]
            if (
                len(limits) != 1
                or nmo.data('parameter', 'show', str(limits[0]['id']), str(path)).get('value')
                != 'false'
            ):
                raise ValueError(f'{kind}: recipe requires the original unlimited hinge')
            body, frame = HINGES[kind]
            m = matrix_of(entities[frame]['world_matrix'])
            length = math.sqrt(sum(v * v for v in m[8:11]))
            recipe.update(
                type='hinge',
                body=body,
                pivot=m[12:15],
                axis=[v / length for v in m[8:11]],
                angles=[-180, 180],
                basis='Native hinge with angular limitations disabled',
            )
        elif kind == 'P_Modul_26':
            top = matrix_of(entities['P_Modul_26_Balljoint_oben']['world_matrix'])[12:15]
            bottom = matrix_of(entities['P_Modul_26_Balljoint_unten']['world_matrix'])[12:15]
            radius = math.dist(top, bottom) + max(
                math.dist(p, bottom) for p in bodies['P_Modul_26_Sack']['points']
            )
            recipe.update(
                type='ball_joint',
                pivot=top,
                radius=radius,
                basis='Two free ball joints: full reach of rope plus sack, independent '
                'of drive phase',
            )
        else:
            recipe.update(
                type='fan',
                body='P_Modul_18_Rotor',
                pivot=[0, -0.55, 0],
                axis=[0, 1, 0],
                angles=[-180, 180],
                effect_body='P_Modul_18_Kollisionsquader',
                basis='Rotor rotation and native force-field collision mesh; effect is not a solid',
            )
        recipes[kind] = recipe
    return {'schema_version': 1, 'recipes': recipes}


def assembly(path, nmo, bodies, entities):
    """Read each physical body's constraint, including joints broken in play.

    Contact, damping and springs are forces, not hard positional limits.
    Unlimited sliders and unconstrained rigid bodies have no finite envelope.
    """
    objects = {o['id']: o for o in nmo.objects(path, 'CKObject')}
    blocks = [
        nmo.data('behavior', 'show', str(b['id']), str(path))
        for b in nmo.data('behavior', 'find', str(path))['results']
    ]

    def parameter(identifier):
        seen = set()
        while identifier not in seen:
            seen.add(identifier)
            obj = objects[identifier]
            fields = field_values(obj)
            if fields.get('source'):
                identifier = fields['source']
            else:
                return obj, fields
        raise ValueError('cyclic native parameter reference')

    def ref(identifier):
        obj, fields = parameter(identifier)
        if fields.get('object_ref'):
            return objects[fields['object_ref']]['name']
        if obj.get('name') == 'FixCube Object':
            return 'FixCube'
        raise ValueError(f'unresolved native object parameter {identifier}')

    def flag(identifier):
        obj, _ = parameter(identifier)
        value = nmo.data('parameter', 'show', str(obj['id']), str(path))['value']
        if value not in ('true', 'false'):
            raise ValueError('expected native boolean')
        return value == 'true'

    contracts = {name: {'type': 'fixed', 'physical': False} for name in bodies}
    joints = []
    for block in blocks:
        name = block['name']
        if name not in ('Physicalize', 'Set Physics Hinge', 'Set Physics Slider'):
            continue
        body = ref(block['target_parameter_id'])
        pins = {p['name']: p['source_id'] for p in block['input_parameters']}
        if name == 'Physicalize':
            fixed = flag(pins['Fixed ?'])
            contracts[body] = {
                'type': 'fixed' if fixed else 'free',
                'physical': True,
                'physicalize': block['id'],
                'start_frozen': flag(pins['Start Frozen']),
            }
            continue
        limited = flag(next(v for k, v in pins.items() if k.startswith('Limitations')))
        if limited:
            raise ValueError(f'{path.stem}: expected native unlimited joint')
        joint = {
            'id': block['id'],
            'body': body,
            'other': ref(pins['Object2']),
            'type': 'hinge' if name.endswith('Hinge') else 'slider',
            'limits_enabled': False,
        }
        if joint['type'] == 'hinge':
            matrix = matrix_of(entities[ref(pins['Joint Referential'])]['world_matrix'])
            axis = matrix[8:11]
            joint.update(
                pivot=matrix[12:15], axis=[v / math.sqrt(sum(x * x for x in axis)) for v in axis]
            )
        else:
            ends = [
                matrix_of(entities[ref(pins[k])]['world_matrix'])[12:15]
                for k in ('Axis first Point', 'Axis second Point')
            ]
            axis = [b - a for a, b in zip(*ends)]
            length = math.sqrt(sum(v * v for v in axis))
            joint.update(pivot=ends[0], axis=[v / length for v in axis])
        joints.append(joint)
    states = ['inactive', 'ready', 'moving']
    transitions = []
    # The bridge's stone-ball branch destroys its seventh joint only; each
    # resulting half remains anchored. Verify the native links we depend on.
    broken = set()
    if path.stem == 'P_Modul_29':
        by_id = {b['id']: b for b in blocks}
        links = [link for b in blocks for link in b.get('behavior_links', [])]
        required = {(700, 692, 306, 303), (306, 303, 188, 172)}
        actual = {
            (link['source_owner_id'], link['in_io_id'], link['target_owner_id'], link['out_io_id'])
            for link in links
        }
        if not required <= actual or by_id[700]['name'] != 'Test':
            raise ValueError('plate bridge break graph changed')
        values = [
            nmo.data('parameter', 'show', str(p['source_id']), str(path)).get('value')
            for p in by_id[700]['input_parameters']
            if p['name'] == 'B'
        ]
        if not any('Ball_Stone' in str(v) for v in values):
            raise ValueError('plate bridge break trigger changed')
        broken = {188}
        states.append('split')
        transitions = [
            {
                'from': 'moving',
                'to': 'split',
                'when': 'native stone-ball proximity test destroys hinge 188',
            },
            {
                'from': 'split',
                'to': 'ready',
                'when': 'sector reset restores bodies and recreates hinges',
            },
            {'from': 'split', 'to': 'inactive', 'when': 'sector deactivated'},
        ]
    for joint in joints:
        if joint['id'] in broken:
            joint['breaks_in_play'] = True
        elif joint['other'] == 'FixCube' and path.stem != 'P_Modul_29':
            contracts[joint['body']].update(
                {k: v for k, v in joint.items() if k in ('type', 'axis', 'pivot')}
            )
    if path.stem == 'P_Modul_29':
        # Traverse each anchored half. Triangle inequality gives a valid
        # sphere for every corner even when all hinges rotate independently.
        remaining = [j for j in joints if j['id'] not in broken]

        def walk(body, previous, pivot, anchor, length, path_ids):
            contract = contracts[body]
            if contract.get('type') == 'reach':
                raise ValueError('expected an acyclic anchored plate chain')
            contract.update(
                type='reach',
                pivot=anchor,
                radius=length + max(math.dist(p, pivot) for p in bodies[body]['points']),
                joint_path=path_ids,
            )
            for j in remaining:
                if j['id'] == previous or body not in (j['body'], j['other']):
                    continue
                other = j['other'] if body == j['body'] else j['body']
                if other == 'FixCube':
                    continue
                walk(
                    other,
                    j['id'],
                    j['pivot'],
                    anchor,
                    length + math.dist(pivot, j['pivot']),
                    path_ids + [j['id']],
                )

        for j in remaining:
            if j['other'] == 'FixCube':
                walk(j['body'], j['id'], j['pivot'], j['pivot'], 0, [j['id']])
        if any(c['physical'] and c['type'] != 'reach' for c in contracts.values()):
            raise ValueError('unanchored plate body')
    return {
        'type': 'assembly',
        'contracts': contracts,
        'joints': joints,
        'states': states,
        'extra_transitions': transitions,
        'solver_tolerance': 0.5,
        'solver_tolerance_basis': 'Per-joint numerical allowance in metres, separate from ideal '
        'reach; not a native hard limit.',
        'basis': 'Native per-body physicalization and unlimited joints; plate '
        'reach includes split state. '
        'Free bodies and unlimited sliders explicitly have no finite global envelope.',
    }
