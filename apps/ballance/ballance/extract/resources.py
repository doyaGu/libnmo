"""Export described resources and record their native sources and runtime bodies."""

import json
import tempfile
from pathlib import Path

from ..core.nmo import field_values, matrix_of
from ..core.project import write_json
from ..core.space import bounds
from ..game.resources import catalog, identity, digest, files, INDEX
from .elements import resource_cut, copy_mesh


def runtime_inventory(nmo, template):
    bodies = []
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        nmo.export_meshes(template, folder)
        for obj in nmo.objects(template, 'CK3dEntity'):
            fields = field_values(obj)
            mesh_id = fields.get('current_mesh')
            if not mesh_id:
                continue
            path = next(folder.glob(f'*_{mesh_id}.obj'))
            points = [
                tuple(map(float, line.split()[1:4]))
                for line in path.read_text().splitlines()
                if line.startswith('v ')
            ]
            bodies.append(
                {
                    'name': obj['name'],
                    'mesh_id': mesh_id,
                    'mesh_bounds': bounds(points),
                    'matrix': matrix_of(fields['world_matrix']),
                    'visibility': fields.get('visibility_flags'),
                }
            )
    return bodies


def export_resources(levels, out, names=None):
    descriptions = catalog()
    names = list(descriptions) if names is None else names
    if not names or len(set(names)) != len(names) or set(names) - descriptions.keys():
        raise ValueError('select distinct known native resources')
    out.mkdir(parents=True, exist_ok=True)
    existing = out / INDEX
    records = (
        json.loads(existing.read_text())
        if existing.is_file()
        else {'schema_version': 1, 'resources': {}}
    )
    if records.get('schema_version') != 1:
        raise ValueError('unsupported resource extraction record')
    found = {}
    for name in names:
        spec = descriptions[name]
        sources = {p: digest(levels.game / p) for p in files(spec)}
        with tempfile.TemporaryDirectory() as tmp:
            staged = Path(tmp)
            centers = {}
            for part in spec['parts']:
                cut = part['extract']
                path = levels.game / cut['file']
                if cut['method'] == 'mesh':
                    obj = levels.objects(path).get(cut['object'])
                    if not obj or not obj.get('current_mesh'):
                        raise ValueError(f'{name}: missing marker {cut["object"]} in {path}')
                    copy_mesh(levels.nmo, path, obj['current_mesh'], part['name'], staged)
                    source = f'{path.name}:{cut["object"]}'
                else:
                    source = resource_cut(part).take(levels, path, staged, centers)
                    if source is None:
                        raise ValueError(f'{name}: missing source objects for {part["name"]}')
                found[part['name']] = source
            assets = {p.name: digest(p) for p in staged.iterdir() if p.suffix in ('.obj', '.mtl')}
            bodies = (
                runtime_inventory(levels.nmo, levels.game / spec['runtime']['template'])
                if 'runtime' in spec
                else []
            )
            if sources != {p: digest(levels.game / p) for p in sources}:
                raise ValueError(f'{name}: native sources changed during extraction')
            for filename in assets:
                # Files can be replaced across filesystems; publish the index last.
                (out / filename).write_bytes((staged / filename).read_bytes())
        records['resources'][name] = {
            'description_sha256': identity(spec),
            'sources': sources,
            'assets': assets,
            'runtime_bodies': bodies,
        }
    write_json(existing, records)
    return found
