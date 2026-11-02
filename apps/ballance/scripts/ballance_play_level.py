#!/usr/bin/env python3
"""Watch a level played in a window, or play it yourself.

    ballance_play_level.py LEVEL [--self] [--mute] [--game DIR] [--nmo NMO]

LEVEL is a folder a level was built in (as ballance_build_level.py writes
it; its Level_01.NMO is patched from manifest.json first when that is newer)
or a level file (Level_NN.NMO).  The level is played as level 1 of the game
installed in DIR ($BALLANCE unless given), with libivp physics (the nmo CLI
built with NMO_IVP_SOURCE_DIR and NMO_VIEWER, $NMO unless given), until the
window is closed.  The game starts the level and skips the tutorial by
itself.

With a route_points.json beside the level, the builder's autopilot rolls
the ball through the level as the verifier plays it, a frame stepping a
fixed 1/60 s; with --self, or without one, the arrow keys do, as in the
game.  Once BALLANCE and NMO are set (in the shell's profile, say):"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.verify.play import watch  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Watch a level played in a window, or play it.'
    )
    parser.add_argument('level', help="A built level's folder, or Level_NN.NMO")
    parser.add_argument(
        '--self',
        action='store_true',
        dest='by_hand',
        help='Roll the ball with the arrow keys instead of the autopilot',
    )
    parser.add_argument('--mute', action='store_true', help='Play no sound')
    parser.add_argument(
        '--game',
        default=os.environ.get('BALLANCE'),
        help='Ballance installation folder (default: $BALLANCE)',
    )
    add_nmo_argument(parser)
    args = parser.parse_args()

    if not args.game or not (Path(args.game) / 'base.cmo').is_file():
        parser.error(
            'give the Ballance installation folder (holding base.cmo) with --game or $BALLANCE'
        )
    source, nmo = Path(args.level), Nmo(args.nmo)
    folder = source if source.is_dir() else source.parent
    level = folder / 'Level_01.NMO' if source.is_dir() else source
    manifest = folder / 'manifest.json'
    if (
        source.is_dir()
        and manifest.is_file()
        and (not level.is_file() or level.stat().st_mtime < manifest.stat().st_mtime)
    ):
        nmo.run('patch', 'apply', '--project', str(manifest), '-o', str(level))
    if not level.is_file():
        parser.error(f'{level} is not there')
    route = folder / 'route_points.json'
    if args.by_hand:
        route = None
    elif not route.is_file():
        print(f'no {route.name} beside the level: roll the ball with the arrow keys')
        route = None
    return watch(level, route, nmo, Path(args.game), args.mute)


if __name__ == '__main__':
    sys.exit(main())
