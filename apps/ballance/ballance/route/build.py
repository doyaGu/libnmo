"""Writing a laid-out route as a level: the folder `nmo patch apply
--project <out>/manifest.json` turns into a level file."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from ..core.obj import Mesh, write_box
from ..game.materials import load_presets
from ..game.measures import FALL_BOTTOM, FALL_MARGIN, FALL_TOP
from . import scenery
from .assets import Assets, MissingMesh
from .layout import Layout, Sector
from .manifest import build_manifest


def write_sector(sector: Sector, index: int, out: Path) -> None:
    """The sector's floors, with the pieces off their grid, as sector<N>.obj,
    and its rails as rails<N>.obj."""
    mesh = Mesh()
    for floors in sector.floors:
        floors.build(mesh)
    for function, (x, z), *rest in sector.extras:
        function(mesh, x, z, *rest)
    mesh.write(out / f"sector{index}.obj")
    if sector.rails.runs:
        sector.rails.write(out / f"rails{index}.obj")


def write_fall_zone(layout: Layout, out: Path) -> None:
    """The box under the whole level that catches a falling ball (depth.obj)."""
    x0, x1, z0, z1 = layout.bounds()
    low = min(layout.lows)
    write_box(out / "depth.obj", (x0 - FALL_MARGIN, x1 + FALL_MARGIN, low - FALL_BOTTOM,
                                  low - FALL_TOP, z0 - FALL_MARGIN, z1 + FALL_MARGIN))


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
        assets = Assets(out, kinds, [kind for kind, _, _, _ in layout.modules], elements)
    except MissingMesh as error:
        print(error, file=sys.stderr)
        return 2
    towers = scenery.towers(layout) if assets.game else []
    sticks = scenery.sticks(layout) if assets.game else []
    for index, sector in enumerate(layout.sectors, start=1):
        write_sector(sector, index, out)
    write_fall_zone(layout, out)
    manifest = build_manifest(load_presets(), assets, layout, name, towers, sticks)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (out / "route_points.json").write_text(json.dumps(layout.points) + "\n", encoding="utf-8")
    print(out / "manifest.json")
    return 0
