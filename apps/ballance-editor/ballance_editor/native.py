"""Real PH meshes and observed poses for the editor; never a physics substitute."""

import csv
from hashlib import sha256
import json
import math
import re
from pathlib import Path

from ballance.core.nmo import field_values, matrix_of
from ballance.core.obj import read_obj
from ballance.core import evidence
from ballance.extract.materials import extract_materials
from ballance.route.motions import catalog

IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def multiply(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def frame(position, yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return [c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, *position, 1]


class NativePreview:
    def __init__(self, game, nmo, cache):
        self.game, self.nmo, self.cache = game, nmo, cache
        self.templates = {}
        self.motion_cache = None

    def template(self, relative):
        path = (self.game / relative).resolve()
        if not path.is_relative_to(self.game.resolve()):
            raise ValueError('Native preview template must belong to the game')
        digest = sha256(path.read_bytes()).hexdigest()
        key = (relative, digest)
        if key in self.templates:
            return self.templates[key]
        # CLI identity also invalidates old mesh exports after encoding fixes.
        cli = Path(self.nmo.executable)
        version = sha256(cli.read_bytes()).hexdigest() if cli.is_file() else self.nmo.executable
        folder = self.cache / sha256((relative + digest + version).encode()).hexdigest()
        folder.mkdir(parents=True, exist_ok=True)
        record = folder / 'preview.json'
        if record.exists():
            data = json.loads(record.read_text())
        else:
            self.nmo.export_meshes(path, folder)
            meshes = {
                int(re.search(r'_(\d+)\.obj$', p.name)[1]): p
                for p in folder.glob('*.obj')
                if re.search(r'_(\d+)\.obj$', p.name)
            }
            materials = extract_materials([path], self.nmo)
            names = {m['name'] for m in materials}
            objects = []
            for o in self.nmo.objects(path, 'CK3dEntity'):
                fields = field_values(o)
                mesh = meshes.get(fields.get('current_mesh'))
                if not mesh:
                    continue
                # PH objects start hidden until activation. Collision helpers have
                # no assigned render material; UFO is a different ending variant.
                if o['name'].startswith('PE_UFO'):
                    continue
                triangles = [t for t in read_obj(mesh) if t.material in names]
                if not triangles:
                    continue
                matrix = matrix_of(fields.get('world_matrix', ''))
                if len(matrix) != 16:
                    raise ValueError('Missing native object transform: ' + o['name'])
                objects.append(
                    {
                        'body': o['name'],
                        'matrix': matrix,
                        'triangles': [t._asdict() for t in triangles],
                    }
                )
            data = {
                'template': relative,
                'sha256': digest,
                'materials': materials,
                'objects': objects,
            }
            record.write_text(json.dumps(data))
        self.templates[key] = data
        return data

    def scene(self, folder):
        if not self.game:
            return [], [], set(), []
        resources = json.loads((folder / 'resources.json').read_text())
        objects = []
        materials = {}
        replaced = set()
        instances = []
        recipes = catalog()
        for instance in resources['instances']:
            spec = resources['resources'][instance['resource']]['description']
            runtime = spec.get('runtime')
            if not runtime:
                continue
            marker_names = [
                n
                for c in instance['components']
                if c['role'] == 'placement_marker'
                for n in c['objects']
            ]
            # FanTower shares a fan recipe, but its actual rotor is a separate Fan
            # instance. Static tower components must not be replaced or duplicated.
            if not marker_names:
                continue
            template = self.template(runtime['template'])
            if not template['objects']:
                continue  # Particle-only marker, no render mesh.
            namespace = template['sha256'][:16] + '/'
            for m in template['materials']:
                materials[namespace + m['name']] = dict(m, name=namespace + m['name'])
            pose = frame(instance['position'], instance['yaw'])
            recipe = recipes.get(runtime.get('motion_recipe'), {})
            instances.append(
                {
                    'id': instance['id'],
                    'parent': instance['parent'],
                    'resource': instance['resource'],
                    'sector': instance.get('base_pose', instance).get('sector', 0),
                    'states': runtime.get('states', []),
                    'bodies': len(template['objects']),
                }
            )
            replaced.update(marker_names)
            for body in template['objects']:
                joint = None
                if recipe.get('body') == body['body'] and recipe.get('type') in ('hinge', 'fan'):
                    joint = {k: recipe[k] for k in ('pivot', 'axis', 'angles')}
                contract = recipe.get('contracts', {}).get(body['body'], {})
                if contract.get('type') == 'hinge':
                    joint = {k: contract[k] for k in ('pivot', 'axis')} | {'angles': [-180, 180]}
                if joint and recipe.get('type') == 'hinge':
                    from ballance.route.dynamics import joint_angles

                    joint = dict(joint, angles=joint_angles(instance, recipe))
                objects.append(
                    {
                        'name': instance['id'] + '/' + body['body'],
                        'body': body['body'],
                        'instance': instance['id'],
                        'parent': instance['parent'],
                        'sector': instance.get('base_pose', instance).get('sector', 0),
                        'matrix': multiply(pose, body['matrix']),
                        'template_matrix': body['matrix'],
                        'instance_matrix': pose,
                        'joint': joint,
                        'triangles': [
                            dict(t, material=namespace + t['material']) for t in body['triangles']
                        ],
                        'native': True,
                        'decoration': spec.get('category') != 'mechanism'
                        and instance['resource'] != 'Balloon',
                    }
                )
        return objects, list(materials.values()), replaced, instances

    def motion(self, folder):
        trace = folder / 'physics.csv'
        reports = [folder / n for n in ('full-validation.json', 'partial-validation.json')]
        trace_digest = sha256(trace.read_bytes()).hexdigest() if trace.exists() else None
        report = next(
            (
                p
                for p in reports
                if p.exists()
                and evidence.inspect(p)['status'] == 'current'
                and json.loads(p.read_text())
                .get('checks', {})
                .get('mechanism_motion', {})
                .get('bodies')
                and json.loads(p.read_text())['checks']['mechanism_motion'].get('trace_sha256')
                == trace_digest
            ),
            None,
        )
        if not report or not trace.exists():
            return {
                'available': False,
                'reason': '先运行当前地图的原生试跑，再回放记录；历史轨迹不会套用到修改后的地图。',
                'tracks': {},
            }
        key = (
            str(folder),
            sha256(report.read_bytes()).hexdigest(),
            trace.stat().st_mtime_ns,
            trace.stat().st_size,
        )
        if self.motion_cache and self.motion_cache[0] == key:
            return self.motion_cache[1]
        result = json.loads(report.read_text())
        motion = result['checks']['mechanism_motion']
        ids = {}
        tracks = {}
        for body in motion['bodies']:
            name = body['mechanism'] + '/' + body['body']
            tracks[name] = {
                'samples': [],
                'active_ranges': body['active_ranges'],
                'first_displaced_frame': body['first_displaced_frame'],
            }
            for identifier in body['runtime_ids']:
                ids[str(identifier)] = name
        last = {}
        finish = result['checks'].get('play', {}).get('finished_frame')
        with trace.open(newline='') as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ['frame', 'id', 'name', *[f'm{k}' for k in range(16)]]:
                raise ValueError('Invalid native pose trace')
            for row in reader:
                keyname = ids.get(row['id'])
                f = int(row['frame'])
                if finish is not None and f > finish:
                    break
                if keyname is None:
                    continue
                matrix = [float(row[f'm{k}']) for k in range(16)]
                if not all(math.isfinite(v) for v in matrix):
                    raise ValueError('Invalid native pose')
                sample = [f, matrix]
                samples = tracks[keyname]['samples']
                previous = last.get(keyname)
                if previous and f > previous[0] + 1 and samples[-1][0] != previous[0]:
                    samples.append(previous)
                if not samples or f % 6 == 0 or (previous and f > previous[0] + 1):
                    samples.append(sample)
                last[keyname] = sample
        for name, sample in last.items():
            if tracks[name]['samples'][-1][0] != sample[0]:
                tracks[name]['samples'].append(sample)
        value = {
            'available': True,
            'tracks': tracks,
            'observed_states': motion.get('observed_states', []),
            'frame_ms': float(result['evidence']['parameters'].get('frame_ms', 16.667)),
            'scope': result['evidence']['parameters'],
            'report': report.name,
            'reason': '原生试跑姿态；未记录的组件保持初始姿态。间断区间不插值。',
        }
        self.motion_cache = (key, value)
        return value
