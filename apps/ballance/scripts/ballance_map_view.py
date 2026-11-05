#!/usr/bin/env python3
"""Draw a Ballance level from any side, with a route and a ball's path on it.

    ballance_map_view.py SOURCE -o view.png [--box X0 X1 Z0 Z1 | --around NAME [R]]
        [--views top,front,side,iso] [--yaw DEG --pitch DEG] [--scale PX]
        [--textures DIR] [--route route_points.json] [--trace steer.csv]

SOURCE is a level file (Level_NN.NMO, read through the nmo CLI) or a folder a
level was built in (manifest.json and its OBJ meshes, as
ballance_build_level.py writes them).  The objects are drawn in their world
places, orthographically, with a depth buffer and flat light, coloured by
what they are: floors (Phys_Floors) as stone, their walls darker, rails
(Phys_FloorRails) as steel, stoppers as wood, the rest grey; with --textures
(the game's Textures folder) the materials' own textures are drawn instead,
except on rails: the game maps their environment texture at run time, so
their UVs follow no rule, and they are drawn as polished steel, shaded
smoothly by their normals with a highlight.
Each mechanism, element and marker (MECHANISMS: the P_* groups the game
fills from 3D Entities/PH, and the start, checkpoints, reset points and
balloon) is drawn in a colour of its own and named beside it in every view,
with its sector; a legend under the views lists those in the image, how
many, and what is known of how each plays.
Hidden objects (the fall zone, the PH placeholders the game hides) are left
out, and so is the SkyLayer.

The views, each a panel of the image:

- top: from above, X to the right and Z up;
- front: from -Z looking along +Z, X to the right and Y up;
- side: from -X looking along +X, -Z to the right and Y up;
- iso: four views 35 degrees down, from each corner;
- view: from --yaw (degrees about Y; 0 looks along +Z) and --pitch (degrees
  down).

Each panel is titled with its view and the axes that run right and up in it;
the single views share a row, and the four iso views have one of their own.

--route draws a builder's route_points.json: the path, its points numbered,
the points the ball stops at (a height to be lifted to, frames to hold)
ringed.  --trace draws the path a ball rolled, as `nmo behavior run
--steer-log` writes it (frame, x, y, z, ...), coloured from slow (blue) to
fast (red), with a frame number every 100 frames.  With a built level, its
route_points.json is drawn unless --route is given.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.core import evidence  # noqa: E402
from ballance.view import scene as scenes  # noqa: E402
from ballance.view.overlays import mechanism_labels, route_overlays, trace_overlays  # noqa: E402
from ballance.view.render import Textures  # noqa: E402
from ballance.view.sheet import around, frame, panels, sheet  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description='Draw a Ballance level from any side.')
    parser.add_argument('source', help='Level_NN.NMO, or a folder with manifest.json')
    parser.add_argument('-o', '--output', required=True, help='PNG to write')
    parser.add_argument('--box', nargs=4, type=float, metavar=('X0', 'X1', 'Z0', 'Z1'))
    parser.add_argument(
        '--around',
        nargs='+',
        metavar=('NAME', 'R'),
        help='Centre on an object, R (default 30) each way',
    )
    parser.add_argument(
        '--heights',
        nargs=2,
        type=float,
        metavar=('Y0', 'Y1'),
        help='Height range (default: the floors in the box)',
    )
    parser.add_argument(
        '--views', default='top,front,side,iso', help='Comma-separated: top, front, side, iso, view'
    )
    parser.add_argument('--yaw', type=float, default=30.0, help="For 'view': degrees about Y")
    parser.add_argument('--pitch', type=float, default=40.0, help="For 'view': degrees down")
    parser.add_argument('--scale', type=float, default=8.0, help='Pixels a unit')
    parser.add_argument('--textures', help="The game's Textures folder, to draw textures")
    parser.add_argument('--route', help='route_points.json to draw')
    parser.add_argument('--trace', help='A --steer-log CSV to draw')
    add_nmo_argument(parser)
    args = parser.parse_args()

    source = Path(args.source)
    dependencies = [Path(p) for p in (args.route, args.trace) if p]
    if args.textures:
        dependencies += [p for p in Path(args.textures).iterdir() if p.is_file()]
    provenance = evidence.capture(source, 'map-preview', vars(args), dependencies)
    scene = scenes.load(source, Nmo(args.nmo))
    # a built level's route, unless another is given
    route_path = Path(args.route) if args.route else source / 'route_points.json'
    route = []
    if args.route or (source.is_dir() and route_path.exists()):
        route = json.loads(route_path.read_text(encoding='utf-8'))
    trace = []
    if args.trace:
        with open(args.trace, newline='', encoding='utf-8') as f:
            trace = list(csv.DictReader(f))

    box = tuple(args.box) if args.box else None
    if args.around:
        box = around(scene, args.around[0], float(args.around[1]) if len(args.around) > 1 else 30.0)
        if box is None:
            print(f'no object {args.around[0]}', file=sys.stderr)
            return 2
    framed = frame(scene, route, box, tuple(args.heights) if args.heights else None)
    labels, present = mechanism_labels(scene, framed.box)
    overlays = route_overlays(route, framed.floor_y) + trace_overlays(trace) + labels
    textures = Textures(Path(args.textures) if args.textures else None)
    try:
        rows = panels(
            scene,
            textures,
            framed,
            [v.strip() for v in args.views.split(',')],
            args.yaw,
            args.pitch,
            args.scale,
            overlays,
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    sheet(rows, present).save(args.output)
    evidence.attach(Path(args.output), provenance)
    print(args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
