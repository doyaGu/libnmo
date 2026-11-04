"""The meshes a built level uses beside its floors: the game's own, copied
from what ballance_level_elements.py exported (--elements), or boxes that
stand in for them."""

from __future__ import annotations

import shutil
import json

from ..game.resources import INDEX, check_export, elements as resource_elements, assemblies, catalog
from pathlib import Path

from ..core.obj import write_box, write_boxes
from ..game.catalog import MARKERS
from ..game.measures import CHECKPOINT_OPENING, FAN_BELOW, FAN_PIT, ROAD_ABOVE_MARKER, START_OPENING
from ..kit.floors import NARROW, WALL
from .modules import MODULE_FLOORS

# The kinds only the game's meshes supply, with nothing standing in for them
# but the floors' boxes below.
GAME_ONLY = ["SkyLayer", "Floor_Start", "Floor_Checkpoint", "Floor_Modul_18", "Pillar", "Tower",
             "Stick"]
# The boxes standing in for the start's and checkpoints' floors (as high as
# their rims), a fan's housing (its rim flush with the floor), and a marker.
STAND_INS = {
    "Floor_Start": ("start_pad.obj", (-START_OPENING, START_OPENING, -WALL, ROAD_ABOVE_MARKER,
                                      -START_OPENING, START_OPENING)),
    "Floor_Checkpoint": ("checkpoint_pad.obj", (-CHECKPOINT_OPENING, CHECKPOINT_OPENING, -WALL,
                                                ROAD_ABOVE_MARKER, -CHECKPOINT_OPENING,
                                                CHECKPOINT_OPENING)),
    "Floor_Modul_18": ("fan_pad.obj", (-NARROW / 2, NARROW / 2, FAN_BELOW - FAN_PIT, FAN_BELOW,
                                       -NARROW / 2, NARROW / 2)),
}
MARKER_BOX = (-0.5, 0.5, 0, 1, -0.5, 0.5)


class MissingMesh(Exception):
    pass


class Assets:
    """The mesh each kind of object in the level is drawn with, by name: the
    game's <kind>.obj, or a stand-in."""

    def __init__(self, out: Path, kinds: list[str], modules: list[str],
                 elements: Path | None) -> None:
        """kinds: the markers, elements and modules the level holds; modules:
        the kinds of module it holds, whose floors it needs too."""
        self.out = out
        self.game = elements is not None
        self.resource_records = {}
        if elements is not None:
            path = elements/INDEX
            if not path.is_file():
                raise MissingMesh(f'no {path}; refresh --elements with ballance_level_elements.py')
            index = json.loads(path.read_text(encoding='utf-8'))
            if index.get('schema_version') != 1:
                raise ValueError('unsupported native resource extraction record')
            required = {'Balloon', 'LanternTower', 'Column', 'FinishPole'}
            required.update(name for kind, (name, _) in resource_elements().items() if kind in kinds or kind in modules)
            required.update(name for kind, (name, _) in assemblies().items() if kind in modules)
            required.update(name for name,spec in catalog().items() if len(spec['parts'])==1 and
                            spec['parts'][0]['name'] in MARKERS+GAME_ONLY)
            for name in sorted(required):
                self.resource_records[name] = check_export(elements,name,index.get('resources',{}).get(name,{}))
        self.meshes: dict[str, str] = {kind: "marker.obj" for kind in MARKERS + kinds}
        floors = [floor for kind in sorted(set(modules)) for floor in MODULE_FLOORS.get(kind, [])]
        for floor in floors:
            self.meshes[floor.name] = f"{floor.name}.obj"
            write_boxes(out / f"{floor.name}.obj", floor.boxes)
        if elements is not None:
            for kind in MARKERS + kinds + GAME_ONLY + [floor.name for floor in floors]:
                self._copy(elements, kind)
        else:
            for name, box in STAND_INS.values():
                write_box(out / name, box)
        write_box(out / "marker.obj", MARKER_BOX)

    def _copy(self, folder: Path, kind: str) -> None:
        for suffix in (".obj", ".mtl"):
            source = folder / f"{kind}{suffix}"
            if suffix == ".obj" and not source.is_file():
                raise MissingMesh(f"no {source}; refresh --elements with ballance_level_elements.py")
            if source.is_file():
                shutil.copyfile(source, self.out / source.name)
        self.meshes[kind] = f"{kind}.obj"

    @classmethod
    def existing(cls, folder: Path):
        """Read-only adapter for generated native or stand-in asset folders."""
        from ..game.catalog import ELEMENT_KINDS
        result=cls.__new__(cls)
        result.out=folder
        result.game=(folder/'Pillar.obj').is_file()
        record=folder/'resources.json'
        result.resource_records = ({name:item['native'] for name,item in json.loads(record.read_text())['resources'].items()
                                    if item['native'] is not None} if record.is_file() else {})
        result.meshes={p.stem:p.name for p in folder.glob('*.obj')}
        for kind in MARKERS+ELEMENT_KINDS:
            result.meshes.setdefault(kind,'marker.obj')
        return result

    def has(self, kind: str) -> bool:
        return kind in self.meshes

    def mesh(self, kind: str) -> str:
        """The game's mesh for kind, or its stand-in."""
        if kind in self.meshes:
            return self.meshes[kind]
        return STAND_INS[kind][0]
