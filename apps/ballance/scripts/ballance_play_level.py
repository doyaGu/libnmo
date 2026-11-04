#!/usr/bin/env python3
"""Watch a level played in a window, or play it yourself.

    ballance_play_level.py LEVEL [--sector N] [--self] [--mute] [--game DIR] [--nmo NMO]

LEVEL is a folder a level was built in (as ballance_build_level.py writes
it; its Level_01.NMO is patched from the current manifest and meshes first)
or a level file (Level_NN.NMO).  The level is played as level 1 with the
game's files in DIR ($BALLANCE unless given), with libivp physics (the nmo
CLI built with NMO_IVP_SOURCE_DIR and NMO_VIEWER, $NMO unless given), until
the window is closed.  It runs in libnmo's test base, not the game's
base.cmo: straight into the level, with no menu or tutorial; with --sector,
from that sector's start with the ball the player has there (a built
level's folder only).

With a route_points.json beside the level, the builder's autopilot rolls
the ball through the level as the verifier plays it, a frame stepping a
fixed 1/60 s; with --self, or without one, the arrow keys do, as in the
game.  Once BALLANCE and NMO are set (in the shell's profile, say):

For a finite preview of the last approach, then manual control:"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.core.project import pack  # noqa: E402
from ballance.verify.play import watch  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Watch a level played in a window, or play it.'
    )
    parser.add_argument('level', help="A built level's folder, or Level_NN.NMO")
    parser.add_argument(
        '--sector',
        type=int,
        default=1,
        help="Start at this sector (a built level's folder only)",
    )
    parser.add_argument(
        '--self',
        action='store_true',
        dest='by_hand',
        help='Roll the ball with the arrow keys instead of the autopilot',
    )
    parser.add_argument('--mute', action='store_true', help='Play no sound')
    parser.add_argument(
        '--handoff-frame',
        type=int,
        metavar='FRAME',
        help='Release the autopilot at this simulation frame for manual play',
    )
    parser.add_argument(
        '--approach',
        action='store_true',
        help='Leave out the final balloon target; requires --handoff-frame',
    )
    parser.add_argument(
        '--shot', type=Path, metavar='PNG', help='Save a native window screenshot'
    )
    parser.add_argument(
        '--shot-frame', type=int, default=4500, help='Screenshot frame (default: 4500)'
    )
    parser.add_argument(
        '--game',
        default=os.environ.get('BALLANCE'),
        help='Ballance installation folder (default: $BALLANCE)',
    )
    add_nmo_argument(parser)
    args = parser.parse_args()

    if not args.game or not (Path(args.game) / '3D Entities').is_dir():
        parser.error(
            'give the Ballance installation folder (holding 3D Entities) with --game '
            'or $BALLANCE'
        )
    source, nmo = Path(args.level), Nmo(args.nmo)
    folder = source if source.is_dir() else source.parent
    level = folder / 'Level_01.NMO' if source.is_dir() else source
    try:
        if source.is_dir():
            level = pack(folder, nmo)
    except (ValueError, RuntimeError, OSError) as error:
        parser.error(str(error))
    if not level.is_file():
        parser.error(f'{level} is not there')
    route = folder / 'route_points.json'
    if args.by_hand:
        route = None
    elif not route.is_file():
        print(f'no {route.name} beside the level: roll the ball with the arrow keys')
        route = None
    if args.sector > 1 and not source.is_dir():
        parser.error("--sector needs a built level's folder")
    try:
        return watch(
            level,
            route,
            nmo,
            Path(args.game),
            args.mute,
            args.sector,
            handoff_frame=args.handoff_frame,
            approach=args.approach,
            shot=args.shot,
            shot_frame=args.shot_frame,
        )
    except (ValueError, RuntimeError, OSError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    sys.exit(main())
