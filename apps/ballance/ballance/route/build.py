"""Writing a laid-out route as a level: the folder `nmo patch apply
--project <out>/manifest.json` turns into a level file."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from ..core.obj import Mesh
from ..core import project
from ..game.materials import load_presets
from ..kit.railings import railing
from . import scenery
from . import attachments, motions
from .motion_stops import prepare as prepare_motion_stops
from .spaces import route_space, SCENERY_MARGIN
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
    if sector.decoration.count:
        sector.decoration.write(out / f"ribs{index}.obj")
    if sector.railings:
        mesh = Mesh()
        for path in sector.railings:
            railing(mesh, path)
        mesh.write(out / f"railings{index}.obj")


def build(route: dict | Path, out: Path, elements: Path | None, name: str) -> int:
    """Build in isolation and publish a complete generation, retaining history."""
    try:
        request = {'source_file': str(route.resolve())} if isinstance(route, Path) else route
        with project.generation(out, request) as staged:
            if isinstance(route, Path):
                route = json.loads(route.read_text(encoding='utf-8'))
            if not isinstance(route, dict):
                raise ValueError('route must be a JSON object')
            _build(route, staged, elements, name)
    except MissingMesh as error:
        print(error, file=sys.stderr)
        return 2
    print(out / 'manifest.json')
    return 0


def _build(route: dict, out: Path, elements: Path | None, name: str) -> int:
    """Lays route out and writes it to out: manifest.json, the OBJ meshes it
    names, and route_points.json.  With elements (a folder of the game's
    meshes, from ballance_level_elements.py) the level uses them and stands
    the scenery; without, boxes stand in.  Returns an exit status."""
    out.mkdir(parents=True, exist_ok=True)
    layout = Layout(route)
    from .gameplay import compile_contracts
    gameplay = compile_contracts(route, layout)
    from ..game.resources import resource
    if layout.last_ball not in resource("Balloon")["needs_ball"]:
        # Every original level turns the ball back to wood before the
        # balloon; none lets a paper or stone ball reach its bridge.
        layout.warn(f"the ball reaches the balloon as {layout.last_ball.lower()}; "
                    "the original levels always turn it back to wood first")
    for message in layout.warnings:
        print(f"warning: {message}", file=sys.stderr)
    kinds = sorted({kind for kind, _, _ in layout.elements} |
                   {kind for kind, _, _, _ in layout.modules})
    assets = Assets(out, kinds,
                    [kind for kind, _, _, _ in layout.modules + layout.module_floors], elements)
    prepare_motion_stops(layout,assets)
    towers = scenery.towers(layout) if assets.game else []
    decoration = route.get('scenery', {})
    if not isinstance(decoration, dict) or set(decoration) - {'finish_poles', 'omit_supports', 'rules', 'instances'}:
        raise ValueError('scenery accepts finish_poles, omit_supports, rules and instances')
    layout.resource_scenery = decoration.get('instances', [])
    # Validate author references even when a stand-in build cannot draw scenery.
    attachments.authored(layout, {attachments.part_id(p):p for p in layout.parts})
    if layout.resource_scenery and not assets.game:
        layout.warn('scenery resource instances need --elements; placement checks skipped')
    rules = decoration.get('rules', {})
    if not isinstance(rules, dict) or any(not isinstance(k,str) or not isinstance(v,dict) for k,v in rules.items()):
        raise ValueError('scenery rules must map accessory IDs to rule objects')
    if rules and not assets.game:
        layout.warn('scenery placement rules need --elements; attachment checks skipped')
    omitted = decoration.get('omit_supports', [])
    if not isinstance(omitted, list) or any(not isinstance(kind, str) for kind in omitted):
        raise ValueError('omit_supports needs a list of module support kinds')
    unknown = set(omitted) - {kind for kind, _, _, _ in layout.supports}
    if unknown:
        raise ValueError(f'unknown module support kinds: {sorted(unknown)}')
    sticks = scenery.sticks(layout, decoration.get('finish_poles')) if assets.game else []
    placements = [{'kind': kind, 'from': position, 'to': None,
                   'action': 'omitted by design', 'reason': 'scenery.omit_supports'}
                  for kind, _, position, _ in layout.supports if kind in omitted]
    layout.supports = [s for s in layout.supports if s[0] not in omitted]
    if assets.game:
        towers, layout.pillars, sticks, fitted = scenery.fit(layout, assets, towers, sticks, rules)
        placements.extend(fitted)
    (out / 'scenery_placement.json').write_text(json.dumps(placements, indent=2) + '\n',
                                               encoding='utf-8')
    for index, sector in enumerate(layout.sectors, start=1):
        write_sector(sector, index, out)
    manifest = build_manifest(load_presets(), assets, layout, name, towers, sticks)
    attachments.bind_objects(layout, manifest)
    from .resources import describe as describe_resources
    project.write_json(out / "resources.json", describe_resources(layout, assets, manifest))
    project.write_json(out / 'spatial_constraints.json', {
        'schema_version': 1, 'native_assets': assets.game, 'margin': SCENERY_MARGIN,
        'occupancy': route_space(layout).data(), 'attachments': layout.scenery_bindings,
        'mechanisms': motions.describe(layout),
        'scope': 'Rail and ball sweeps, native finish motion, scenery assemblies and attachment contacts. '
                 'Native mechanism sweeps and force fields use measured recipes. '
                 'Bounds are conservative; unsupported mechanisms and camera envelopes remain unmodeled.'})
    # Touching one of these boxes is what makes the ball fall off ("Ball
    # Off"); the original fall zones are hidden.
    objects = manifest["scenes"][0]["objects"]
    for index, mesh in enumerate(write_fall_zones(objects, out, route.get('death_zones')), start=1):
        objects.append(level_object(f"Quader{index:02d}", mesh, [0, 0, 0], ["DepthTestCubes"],
                                    visible=False))
    conflicts = clashes(layout, assets)
    for message in conflicts:
        print(f"warning: {message}", file=sys.stderr)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (out / "route_points.json").write_text(json.dumps(layout.points) + "\n", encoding="utf-8")
    (out / "route_sectors.json").write_text(json.dumps(layout.sector_starts) + "\n",
                                            encoding="utf-8")
    (out / "route_plan.txt").write_text(layout.plan(), encoding="utf-8")
    (out / 'source_map.json').write_text(json.dumps(layout.source_map(), indent=2) + '\n',
                                        encoding='utf-8')
    project.write_json(out / 'connections.json', layout.connection_report())
    project.write_json(out / 'gameplay_contracts.json', gameplay)
    if assets.game:
        from ..view.scene import load_project
        from ..verify.scenery import check
        findings = check(load_project(out, hidden=True), layout, layout.scenery_bindings)
        if findings:
            first = findings[0]
            raise ValueError(f"exported scenery failed {first.get('rule', first['kind'])}: "
                             f"{first['object']} ({len(findings)} findings)")
    project.record(out, route, layout.warnings + conflicts, assets.game)
    return 0
