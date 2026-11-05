"""The objects of a level to draw: from a level file, read through the nmo
CLI (and kept in a cache by the file's time), or from the folder a level
was built in (its manifest.json and OBJ meshes)."""

from __future__ import annotations

import json
import math
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path

from ..core.nmo import Nmo, field_values, matrix_of, place, turn
from ..core.obj import Triangle, read_obj

# Hidden objects are left out, as the game does; and so is the cloud plane.
LEFT_OUT = {'SkyLayer'}


@dataclass
class SceneObject:
    name: str
    groups: list[str]
    triangles: list[Triangle]  # in the world
    at: tuple[float, float, float]  # its position


def load(source: Path, nmo: Nmo, hidden: bool = False) -> list[SceneObject]:
    """The visible objects of a level file, or of a folder holding a built
    level's manifest.json; with hidden, the hidden ones too (the invisible
    floors the ball still rolls against)."""
    scene = load_project(source, hidden) if source.is_dir() else load_level(source, nmo, hidden)
    return [obj for obj in scene if obj.name not in LEFT_OUT]


def load_level(path: Path, nmo: Nmo, hidden: bool = False) -> list[SceneObject]:
    cache = (
        Path(tempfile.gettempdir())
        / 'ballance_map_view'
        / f'{path.stem}-{int(path.stat().st_mtime)}'
    )
    if not (cache / 'objects.json').exists():
        (cache / 'meshes').mkdir(parents=True, exist_ok=True)
        nmo.export_meshes(path, cache / 'meshes')
        groups: dict[str, list[str]] = {}
        for group, members in nmo.groups(path).items():
            for member in members:
                groups.setdefault(member['name'], []).append(group)
        objects = []
        for obj in nmo.objects(path, 'CK3dObject'):
            fields = field_values(obj)
            objects.append(
                {
                    'name': obj['name'],
                    'mesh': fields.get('current_mesh'),
                    'matrix': matrix_of(fields.get('world_matrix') or ''),
                    'visible': fields.get('visibility_flags') != 'CK_OBJECT_FLAGS(0x0)',
                    'groups': groups.get(obj['name'], []),
                }
            )
        (cache / 'objects.json').write_text(json.dumps(objects), encoding='utf-8')
    objects = json.loads((cache / 'objects.json').read_text(encoding='utf-8'))
    meshes = {}
    for obj_path in (cache / 'meshes').glob('*.obj'):
        found = re.search(r'_(\d+)\.obj$', obj_path.name)
        if found:
            meshes[int(found.group(1))] = obj_path
    scene = []
    for obj in objects:
        mesh, m = obj['mesh'], obj['matrix']
        if not (obj['visible'] or hidden) or not mesh or int(mesh) not in meshes or len(m) < 16:
            continue
        triangles = [
            Triangle(
                t.material,
                [place(m, p) for p in t.points],
                t.uvs,
                [turn(m, n) for n in t.normals] if t.normals else t.normals,
            )
            for t in read_obj(meshes[int(mesh)])
        ]
        scene.append(SceneObject(obj['name'], obj['groups'], triangles, (m[12], m[13], m[14])))
    return scene


def load_project(folder: Path, hidden: bool = False) -> list[SceneObject]:
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    scene = []
    for scn in manifest.get('scenes', []):
        for obj in scn.get('objects', []):
            mesh = (obj.get('mesh') or {}).get('obj')
            if (
                not mesh
                or (obj.get('visible') is False and not hidden)
                or not (folder / mesh).exists()
            ):
                continue
            pos = obj.get('transform', {}).get('position', [0, 0, 0])
            angle = obj.get('transform', {}).get('rotation_euler_deg', [0, 0, 0])[1]
            c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))

            def turned(p, w):
                # a turn about Y: 90 degrees takes X to -Z
                return (
                    p[0] * c + p[2] * s + pos[0] * w,
                    p[1] + pos[1] * w,
                    -p[0] * s + p[2] * c + pos[2] * w,
                )

            triangles = [
                Triangle(
                    t.material,
                    [turned(p, 1) for p in t.points],
                    t.uvs,
                    [turned(n, 0) for n in t.normals] if t.normals else t.normals,
                )
                for t in read_obj(folder / mesh)
            ]
            scene.append(SceneObject(obj['name'], obj.get('groups', []), triangles, tuple(pos)))
    return scene
