"""Writing a laid-out route as a level: the folder `nmo patch apply
--project <out>/manifest.json` turns into a level file."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from ..core.obj import Mesh
from ..game.materials import load_presets
from ..kit.railings import railing
from . import scenery
from .falls import write_fall_zones
from .assets import Assets, MissingMesh
from .clashes import clashes
from .layout import Layout, Sector
from .manifest import build_manifest, level_object


def write_sector(sector: Sector, index: int, out: Path) -> None:
    """The sector's floors, with the pieces off their grid, as sector<N>.obj,
    its rails as rails<N>.obj and its railings as railings<N>.obj."""
    mesh = Mesh()
    for floors in sector.floors:
        floors.build(mesh)
    for function, (x, z), *rest in sector.extras:
        function(mesh, x, z, *rest)
    mesh.write(out / f"sector{index}.obj")
    if sector.rails.runs:
        sector.rails.write(out / f"rails{index}.obj")
    if sector.railings:
        mesh = Mesh()
        for path in sector.railings:
            railing(mesh, path)
        mesh.write(out / f"railings{index}.obj")


def build(route: dict, out: Path, elements: Path | None, name: str) -> int:
    """Lays route out and writes it to out: manifest.json, the OBJ meshes it
    names, and route_points.json.  With elements (a folder of the game's
    meshes, from ballance_level_elements.py) the level uses them and stands
    the scenery; without, boxes stand in.  Returns an exit status."""
    out.mkdir(parents=True, exist_ok=True)
    layout = Layout(route)
    if layout.last_ball != "Wood":
        # Every original level turns the ball back to wood before the
        # balloon; none lets a paper or stone ball reach its bridge.
        layout.warn(f"the ball reaches the balloon as {layout.last_ball.lower()}; "
                    "the original levels always turn it back to wood first")
    for message in layout.warnings:
        print(f"warning: {message}", file=sys.stderr)
    kinds = sorted({kind for kind, _, _ in layout.elements} |
                   {kind for kind, _, _, _ in layout.modules})
    try:
        assets = Assets(out, kinds,
                        [kind for kind, _, _, _ in layout.modules + layout.module_floors], elements)
    except MissingMesh as error:
        print(error, file=sys.stderr)
        return 2
    towers = scenery.towers(layout) if assets.game else []
    sticks = scenery.sticks(layout) if assets.game else []
    for index, sector in enumerate(layout.sectors, start=1):
        write_sector(sector, index, out)
    manifest = build_manifest(load_presets(), assets, layout, name, towers, sticks)
    # Touching one of these boxes is what makes the ball fall off ("Ball
    # Off"); the original fall zones are hidden.
    objects = manifest["scenes"][0]["objects"]
    for index, mesh in enumerate(write_fall_zones(objects, out), start=1):
        objects.append(level_object(f"Quader{index:02d}", mesh, [0, 0, 0], ["DepthTestCubes"],
                                    visible=False))
    for message in clashes(layout, assets):
        print(f"warning: {message}", file=sys.stderr)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (out / "route_points.json").write_text(json.dumps(layout.points) + "\n", encoding="utf-8")
    (out / "route_sectors.json").write_text(json.dumps(layout.sector_starts) + "\n",
                                            encoding="utf-8")
    (out / "route_plan.txt").write_text(layout.plan(), encoding="utf-8")
    print(out / "manifest.json")
    return 0
