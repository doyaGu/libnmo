"""The project manifest of a laid-out route: its objects, groups and
materials, for `nmo patch apply --project`."""

from __future__ import annotations

from ..game.resources import resource, elements as resource_elements

from pathlib import Path

from ..core.geometry import turn_of
from ..game.catalog import LEVEL_GROUPS, RAIL_GROUPS, RAILING_GROUPS, STONE_FLOOR_GROUPS
from ..game.measures import FIRST_RESET_ABOVE, RESET_ABOVE, SKY_BELOW
from .assets import Assets
from .layout import Layout
from .modules import MODULE_FLOORS

# The materials every level's floors use.
FLOOR_MATERIALS = {
    'Floor_Top_Profile',
    'Floor_Top_ProfileFlat',
    'Floor_Top_Flat',
    'Floor_Top_Boderless',
    'Floor_Top_Border',
    'Floor_Side',
}
# The materials of the scenery.
SCENERY_MATERIALS = [
    'Colums_blue',
    'Colums_beige',
    'Columns_beige_fade',
    'Tower_Floor_Side',
    'Tower_Floor_Top_Flat_illu',
    'TowerRail',
    'TowerRoof',
    'TowerRoof_Brim',
    'Laterne_Glas',
    'Laterne_Metal',
    'Laterne_Schatten',
    'Laterne_Verlauf',
    'Stick_Bottom',
    'Stick_Green',
    'Stick_Rail',
    'Stick_Red',
    'Stick_Stripes',
]
# The start's flames take their texture by name from the checkpoints' file
# (PH/PC_TwoFlames.nmo), which the game loads only for a level with a
# checkpoint; a level without one carries it, or its flames are white.
FLAMES_TEXTURE = {
    'name': 'Particle_Flames',
    'texture': 'Particle_Flames.bmp',
    'texture_format': 'rgb565',
    'texture_save': 'external',
}


def level_object(
    name: str,
    mesh: str,
    position: list[float],
    groups: list[str],
    visible: bool = True,
    turn: float = 0.0,
) -> dict:
    """An object of the manifest; turn is in degrees about Y (90 turns its
    X axis to -Z, CK's Y being left-handed)."""
    obj = {
        'name': name,
        'class': 'CK3dObject',
        'mesh': {'obj': mesh},
        'transform': {'position': position},
        'groups': groups,
    }
    if turn % 360:
        obj['transform']['rotation_euler_deg'] = [0, turn % 360, 0]
    if not visible:
        obj['visible'] = False
    return obj


class Manifest:
    """The objects and materials, gathered as the level's parts are added."""

    def __init__(self) -> None:
        self.objects: list[dict] = []
        self.materials: set[str] = set(FLOOR_MATERIALS)
        self.counts: dict[str, int] = {}

    def add(self, *args, **kwargs) -> None:
        self.objects.append(level_object(*args, **kwargs))

    def number(self, kind: str) -> int:
        """The next number for an object of kind: 1, 2, ..."""
        self.counts[kind] = self.counts.get(kind, 0) + 1
        return self.counts[kind]


def build_manifest(
    presets: list[dict],
    assets: Assets,
    layout: Layout,
    name: str,
    towers: list[list[float]],
    sticks: list[list[float]],
) -> dict:
    """The manifest, with materials from presets (ballance.game.materials)."""
    manifest = Manifest()
    _markers(manifest, assets, layout)
    _floors(manifest, assets, layout)
    _elements(manifest, assets, layout)
    _pads(manifest, assets, layout)
    if assets.has('Pillar'):
        _scenery(manifest, assets, layout, towers, sticks)
    if assets.has('SkyLayer'):
        # The cloud plane under the level, as far below as the originals'.
        x0, x1, z0, z1 = layout.bounds()
        manifest.add(
            'SkyLayer', assets.mesh('SkyLayer'), [(x0 + x1) / 2, -SKY_BELOW, (z0 + z1) / 2], []
        )
        manifest.materials.add('SkyLayer')
    return {
        'version': 1,
        'document': {'name': name},
        'materials': [m for m in presets if m['name'] in manifest.materials]
        + ([] if layout.checkpoints else [FLAMES_TEXTURE]),
        # every sector's group, even one with no element in it: without
        # Sector_01 the game never sets the balloon up, and the ball falls
        # through where its planks should be
        'groups': [{'name': group} for group in LEVEL_GROUPS]
        + [{'name': f'Sector_{index:02d}'} for index in range(1, len(layout.sectors) + 1)],
        'scenes': [{'name': name, 'objects': manifest.objects}],
    }


def _markers(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    # The turns are Level_03's, whose road runs along +X, turned with the
    # track: the start is turned 90 degrees from it; a reset point's -X looks
    # along the track.
    heading = turn_of(layout.heading)
    sx, sz = layout.start
    manifest.add(
        'PS_FourFlames_01',
        assets.mesh('PS_FourFlames'),
        [sx, layout.marker_y, sz],
        ['PS_Levelstart'],
        turn=heading - 90,
    )
    manifest.add(
        'PR_Resetpoint_01',
        assets.mesh('PR_Resetpoint'),
        [sx, layout.marker_y + FIRST_RESET_ABOVE, sz],
        ['PR_Resetpoints'],
        turn=heading + 180,
    )


def _floors(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    for index, sector in enumerate(layout.sectors, start=1):
        if sector.decoration.count:
            manifest.add(f'Support_Ribs_{index:02d}', f'ribs{index}.obj', [0, 0, 0], [])
            manifest.materials.add('Rail')
        manifest.add(f'A{index:02d}_Floor_02', f'sector{index}.obj', [0, 0, 0], STONE_FLOOR_GROUPS)
        if sector.rails.runs:
            manifest.add(f'A{index:02d}_Rail_01', f'rails{index}.obj', [0, 0, 0], RAIL_GROUPS)
            manifest.materials.add('Rail')
        if sector.railings:
            manifest.add(
                f'A{index:02d}_Railing_01', f'railings{index}.obj', [0, 0, 0], RAILING_GROUPS
            )
            manifest.materials.add('Rail')
    for index, (sector, position) in enumerate(layout.housings, start=1):
        manifest.add(
            f'A{sector:02d}_Floor_Modul_18_{index:02d}',
            assets.mesh('Floor_Modul_18'),
            position,
            STONE_FLOOR_GROUPS,
        )
    if layout.housings and assets.has('Floor_Modul_18'):
        manifest.materials.update(('Modul18_Top', 'Modul18_Gitter'))
    # The checkpoints turn with the track; the balloon's planks point back
    # along it.
    for index, (marker, d) in enumerate(layout.checkpoints, start=1):
        manifest.add(
            f'PC_TwoFlames_{index:02d}',
            assets.mesh('PC_TwoFlames'),
            marker,
            ['PC_Checkpoints'],
            turn=turn_of(d),
        )
        manifest.add(
            f'PR_Resetpoint_{index + 1:02d}',
            assets.mesh('PR_Resetpoint'),
            [marker[0], marker[1] + RESET_ABOVE, marker[2]],
            ['PR_Resetpoints'],
            turn=turn_of(d) + 180,
        )
    where, d = layout.balloon
    balloon = resource('Balloon')
    marker = balloon['parts'][0]
    manifest.add(
        'PE_Balloon_01',
        assets.mesh(marker['name']),
        where,
        marker['groups'],
        turn=turn_of(d) + balloon['placement']['yaw_offset'],
    )


def _elements(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    descriptions = resource_elements()
    for entry in [*layout.elements, *layout.modules]:
        kind, sector, position, *rotation = entry
        turn = rotation[0] if rotation else 0
        pose = layout.mechanism_entries.get(id(entry))
        if pose:
            sector, position, turn = pose['sector'], pose['position'], pose['yaw']
        groups = descriptions[kind][1]['parts'][0]['groups'] if kind in descriptions else [kind]
        if pose and pose.get('runtime_variant'):
            groups = [pose['runtime_variant'] if g == kind else g for g in groups]
        manifest.add(
            f'{kind}_{manifest.number(kind):02d}',
            assets.mesh(kind),
            position,
            [*groups, f'Sector_{sector:02d}'],
            turn=turn,
        )
    for kind, sector, position, turn in layout.module_floors:
        for floor in MODULE_FLOORS[kind]:
            if floor.support:
                continue
            name = f'A{sector:02d}_{floor.name}_{manifest.number(floor.name):02d}'
            manifest.add(
                name, assets.mesh(floor.name), position, floor.groups, visible=floor.visible, turn=turn
            )
            # a floor cut from a level brings the level's materials with it
            manifest.materials.update(_mesh_materials(assets.out / assets.mesh(floor.name)))
            if floor.visible and 'Phys_Floors' in floor.groups:
                _add_underside(manifest, assets, name, floor.name, position, turn)
    for kind, sector, position, turn in layout.supports:
        manifest.add(
            f'A{sector:02d}_{kind}_{manifest.number(kind):02d}',
            assets.mesh(kind),
            position,
            [],
            turn=turn,
        )
        manifest.materials.update(_mesh_materials(assets.out / assets.mesh(kind)))
    manifest.materials.update(f'Floor_Trafo_{kind}' for kind in layout.trafos)
    if layout.modules:
        manifest.materials.add('Floor_Stopper_Wood')


# How far below a floor's top its underside lies: the walls' depth.
UNDERSIDE_BELOW = 5.0
# The stone floors' top materials an underside is laid under; wood (logs,
# troughs, planks, walkways) is a thin shell with nothing under it.
STONE_TOPS = ('Floor_Top', 'Floor_Trafo', 'Modul01_Floor')
# Floors set into another floor that has its own underside.
NO_UNDERSIDE = {'Floor_Modul_18'}


def _add_underside(
    manifest: Manifest, assets: Assets, name: str, kind: str, position, turn: float
) -> None:
    """The underside of a floor cut from a level, as an object of its own
    beside it, only seen (no physics group): the originals' floors have no
    bottom, as their camera never looks up at one, and a level that stacks
    its floors shows them open from below."""
    if kind in NO_UNDERSIDE:
        return
    under = f'{kind}_Under.obj'
    path = assets.out / under
    if not path.exists():
        if not underside_mesh(assets.out / assets.mesh(kind), path):
            return
    manifest.add(f'{name}_Under', under, position, [], turn=turn)
    manifest.materials.add('Floor_Side')


def underside_mesh(source: Path, out: Path, below: float = UNDERSIDE_BELOW) -> bool:
    """Writes to out the faces of the OBJ at source that face up, moved below
    down and turned to face down, in Floor_Side; False if none face up."""
    points, normals, lines = [], [], ['usemtl Floor_Side']
    count = 0
    material = ''
    for line in source.read_text(encoding='utf-8').splitlines():
        head, *rest = line.split() or ['']
        if head == 'usemtl':
            material = rest[0] if rest else ''
        elif head == 'v':
            points.append(tuple(float(v) for v in rest[:3]))
        elif head == 'vn':
            normals.append(tuple(float(v) for v in rest[:3]))
        elif head == 'f':
            refs = [ref.split('/') for ref in rest]

            def index(value: str, items: list) -> int:
                k = int(value)
                return len(items) + k if k < 0 else k - 1

            corners = [points[index(r[0], points)] for r in refs]
            if len(refs[0]) > 2 and refs[0][2]:
                up = sum(normals[index(r[2], normals)][1] for r in refs) / len(refs)
            else:
                a, b, c = corners[:3]
                up = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2])
            if up <= 0.5 or not material.startswith(STONE_TOPS):
                continue
            for k in range(1, len(corners) - 1):
                # reversed: facing down
                for x, y, z in (corners[0], corners[k + 1], corners[k]):
                    lines.append(f'v {x:.6f} {y - below:.6f} {z:.6f}')
                    lines.append(f'vt {x / 5.0:.6f} {z / 5.0:.6f}')
                    lines.append('vn 0 -1 0')
                lines.append('f -3/-3/-3 -2/-2/-2 -1/-1/-1')
                count += 1
    if not count:
        return False
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return True


def _mesh_materials(path: Path) -> set[str]:
    """The materials an OBJ mesh uses."""
    return {
        line.split()[1]
        for line in path.read_text(encoding='utf-8').splitlines()
        if line.startswith('usemtl ')
    }


def _pads(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    """The start's and the checkpoints' floors, turned with the track (their
    stand-ins unturned)."""
    sx, sz = layout.start
    pads = [('A01_Floor_01', [sx, layout.marker_y, sz], turn_of(layout.heading), 'Floor_Start')]
    for index, (marker, d) in enumerate(layout.checkpoints, start=2):
        pads.append((f'A{index:02d}_Floor_01', list(marker), turn_of(d), 'Floor_Checkpoint'))
    for name, position, turn, kind in pads:
        game = assets.has(kind)
        manifest.add(
            name, assets.mesh(kind), position, STONE_FLOOR_GROUPS, turn=turn if game else 0
        )
        if game:
            _add_underside(manifest, assets, name, kind, position, turn)
    if assets.has('Floor_Start'):
        manifest.materials.add('Floor_Top_Checkpoint')


def _scenery(
    manifest: Manifest,
    assets: Assets,
    layout: Layout,
    towers: list[list[float]],
    sticks: list[list[float]],
) -> None:
    # Lantern towers are floors the ball can hit, as in the originals; pillars
    # and poles are only seen.
    parts = {
        p['name']: p
        for name in ('LanternTower', 'Column', 'FinishPole')
        for p in resource(name)['parts']
    }
    for index, position in enumerate(towers, start=1):
        manifest.add(f'Tower_{index:02d}', assets.mesh('Tower'), position, parts['Tower']['groups'])
    for index, position in enumerate(towers + layout.pillars, start=1):
        manifest.add(
            f'Floor_Columns_{index:02d}', assets.mesh('Pillar'), position, parts['Pillar']['groups']
        )
    for index, position in enumerate(sticks, start=1):
        manifest.add(f'Stick_{index:02d}', assets.mesh('Stick'), position, parts['Stick']['groups'])
    turns = {
        name: binding['turn']
        for binding in layout.scenery_bindings
        if binding['role'] in ('lamp', 'pillar')
        for name in binding['objects']
    }
    for obj in manifest.objects:
        if turns.get(obj['name'], 0) % 360:
            obj['transform']['rotation_euler_deg'] = [0, turns[obj['name']] % 360, 0]
    if assets.has('Scenery_Braces'):
        manifest.add('Support_Scenery', assets.mesh('Scenery_Braces'), [0, 0, 0], [])
        manifest.materials.add('Rail')
    manifest.materials.update(SCENERY_MATERIALS)
