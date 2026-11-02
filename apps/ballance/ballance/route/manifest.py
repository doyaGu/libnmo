"""The project manifest of a laid-out route: its objects, groups and
materials, for `nmo patch apply --project`."""

from __future__ import annotations

from pathlib import Path

from ..core.geometry import turn_of
from ..game.catalog import LEVEL_GROUPS, RAIL_GROUPS, STONE_FLOOR_GROUPS
from ..game.measures import FIRST_RESET_ABOVE, RESET_ABOVE, SKY_BELOW
from .assets import Assets
from .layout import Layout
from .modules import MODULE_FLOORS

# The materials every level's floors use.
FLOOR_MATERIALS = {"Floor_Top_Profile", "Floor_Top_ProfileFlat", "Floor_Top_Flat",
                   "Floor_Top_Boderless", "Floor_Side"}
# The materials of the scenery.
SCENERY_MATERIALS = [
    "Colums_blue", "Colums_beige", "Columns_beige_fade", "Tower_Floor_Side",
    "Tower_Floor_Top_Flat_illu", "TowerRail", "TowerRoof", "TowerRoof_Brim", "Laterne_Glas",
    "Laterne_Metal", "Laterne_Schatten", "Laterne_Verlauf", "Stick_Bottom", "Stick_Green",
    "Stick_Rail", "Stick_Red", "Stick_Stripes"]
# The start's flames take their texture by name from the checkpoints' file
# (PH/PC_TwoFlames.nmo), which the game loads only for a level with a
# checkpoint; a level without one carries it, or its flames are white.
FLAMES_TEXTURE = {"name": "Particle_Flames", "texture": "Particle_Flames.bmp",
                  "texture_format": "rgb565", "texture_save": "external"}


def level_object(name: str, mesh: str, position: list[float],
                 groups: list[str], visible: bool = True, turn: float = 0.0) -> dict:
    """An object of the manifest; turn is in degrees about Y (90 turns its
    X axis to -Z, CK's Y being left-handed)."""
    obj = {
        "name": name,
        "class": "CK3dObject",
        "mesh": {"obj": mesh},
        "transform": {"position": position},
        "groups": groups,
    }
    if turn % 360:
        obj["transform"]["rotation_euler_deg"] = [0, turn % 360, 0]
    if not visible:
        obj["visible"] = False
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


def build_manifest(presets: list[dict], assets: Assets, layout: Layout, name: str,
                   towers: list[list[float]], sticks: list[list[float]]) -> dict:
    """The manifest, with materials from presets (ballance.game.materials)."""
    manifest = Manifest()
    _markers(manifest, assets, layout)
    _floors(manifest, assets, layout)
    _elements(manifest, assets, layout)
    _pads(manifest, assets, layout)
    if assets.has("Pillar"):
        _scenery(manifest, assets, layout, towers, sticks)
    if assets.has("SkyLayer"):
        # The cloud plane under the level, as far below as the originals'.
        x0, x1, z0, z1 = layout.bounds()
        manifest.add("SkyLayer", assets.mesh("SkyLayer"),
                     [(x0 + x1) / 2, -SKY_BELOW, (z0 + z1) / 2], [])
        manifest.materials.add("SkyLayer")
    return {
        "version": 1,
        "document": {"name": name},
        "materials": [m for m in presets if m["name"] in manifest.materials] +
                     ([] if layout.checkpoints else [FLAMES_TEXTURE]),
        # every sector's group, even one with no element in it: without
        # Sector_01 the game never sets the balloon up, and the ball falls
        # through where its planks should be
        "groups": [{"name": group} for group in LEVEL_GROUPS] +
                  [{"name": f"Sector_{index:02d}"} for index in range(1, len(layout.sectors) + 1)],
        "scenes": [{"name": name, "objects": manifest.objects}],
    }


def _markers(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    # The turns are Level_03's, whose road runs along +X, turned with the
    # track: the start is turned 90 degrees from it; a reset point's -X looks
    # along the track.
    heading = turn_of(layout.heading)
    sx, sz = layout.start
    manifest.add("PS_FourFlames_01", assets.mesh("PS_FourFlames"), [sx, layout.marker_y, sz],
                 ["PS_Levelstart"], turn=heading - 90)
    manifest.add("PR_Resetpoint_01", assets.mesh("PR_Resetpoint"),
                 [sx, layout.marker_y + FIRST_RESET_ABOVE, sz], ["PR_Resetpoints"],
                 turn=heading + 180)


def _floors(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    for index, sector in enumerate(layout.sectors, start=1):
        manifest.add(f"A{index:02d}_Floor_02", f"sector{index}.obj", [0, 0, 0],
                     STONE_FLOOR_GROUPS)
        if sector.rails.runs:
            manifest.add(f"A{index:02d}_Rail_01", f"rails{index}.obj", [0, 0, 0], RAIL_GROUPS)
            manifest.materials.add("Rail")
    for index, (sector, position) in enumerate(layout.housings, start=1):
        manifest.add(f"A{sector:02d}_Floor_Modul_18_{index:02d}", assets.mesh("Floor_Modul_18"),
                     position, STONE_FLOOR_GROUPS)
    if layout.housings and assets.has("Floor_Modul_18"):
        manifest.materials.update(("Modul18_Top", "Modul18_Gitter"))
    # The checkpoints turn with the track; the balloon's planks point back
    # along it.
    for index, (marker, d) in enumerate(layout.checkpoints, start=1):
        manifest.add(f"PC_TwoFlames_{index:02d}", assets.mesh("PC_TwoFlames"), marker,
                     ["PC_Checkpoints"], turn=turn_of(d))
        manifest.add(f"PR_Resetpoint_{index + 1:02d}", assets.mesh("PR_Resetpoint"),
                     [marker[0], marker[1] + RESET_ABOVE, marker[2]], ["PR_Resetpoints"],
                     turn=turn_of(d) + 180)
    where, d = layout.balloon
    manifest.add("PE_Balloon_01", assets.mesh("PE_Balloon"), where, ["PE_Levelende"],
                 turn=turn_of(d) + 180)


def _elements(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    for kind, sector, position, turn in [e + (0.0,) for e in layout.elements] + layout.modules:
        manifest.add(f"{kind}_{manifest.number(kind):02d}", assets.mesh(kind), position,
                     [kind, f"Sector_{sector:02d}"], turn=turn)
    for kind, sector, position, turn in layout.module_floors:
        for floor in MODULE_FLOORS[kind]:
            manifest.add(f"A{sector:02d}_{floor.name}_{manifest.number(floor.name):02d}",
                         assets.mesh(floor.name), position, floor.groups,
                         visible=floor.visible, turn=turn)
            # a floor cut from a level brings the level's materials with it
            manifest.materials.update(_mesh_materials(assets.out / assets.mesh(floor.name)))
    manifest.materials.update(f"Floor_Trafo_{kind}" for kind in layout.trafos)
    if layout.modules:
        manifest.materials.add("Floor_Stopper_Wood")


def _mesh_materials(path: Path) -> set[str]:
    """The materials an OBJ mesh uses."""
    return {line.split()[1] for line in path.read_text(encoding="utf-8").splitlines()
            if line.startswith("usemtl ")}


def _pads(manifest: Manifest, assets: Assets, layout: Layout) -> None:
    """The start's and the checkpoints' floors, turned with the track (their
    stand-ins unturned)."""
    sx, sz = layout.start
    pads = [("A01_Floor_01", [sx, layout.marker_y, sz], turn_of(layout.heading), "Floor_Start")]
    for index, (marker, d) in enumerate(layout.checkpoints, start=2):
        pads.append((f"A{index:02d}_Floor_01", list(marker), turn_of(d), "Floor_Checkpoint"))
    for name, position, turn, kind in pads:
        game = assets.has(kind)
        manifest.add(name, assets.mesh(kind), position, STONE_FLOOR_GROUPS,
                     turn=turn if game else 0)
    if assets.has("Floor_Start"):
        manifest.materials.add("Floor_Top_Checkpoint")


def _scenery(manifest: Manifest, assets: Assets, layout: Layout, towers: list[list[float]],
             sticks: list[list[float]]) -> None:
    # Lantern towers are floors the ball can hit, as in the originals; pillars
    # and poles are only seen.
    for index, position in enumerate(towers, start=1):
        manifest.add(f"Tower_{index:02d}", assets.mesh("Tower"), position, ["Phys_Floors"])
    for index, position in enumerate(towers + layout.pillars, start=1):
        manifest.add(f"Floor_Columns_{index:02d}", assets.mesh("Pillar"), position, [])
    for index, position in enumerate(sticks, start=1):
        manifest.add(f"Stick_{index:02d}", assets.mesh("Stick"), position, [])
    manifest.materials.update(SCENERY_MATERIALS)
